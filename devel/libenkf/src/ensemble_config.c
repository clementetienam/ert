/*
   copyright (c) 2011  statoil asa, norway. 
    
   the file 'ensemble_config.c' is part of ert - ensemble based reservoir tool. 
    
   ert is free software: you can redistribute it and/or modify 
   it under the terms of the gnu general public license as published by 
   the free software foundation, either version 3 of the license, or 
   (at your option) any later version. 
    
   ert is distributed in the hope that it will be useful, but without any 
   warranty; without even the implied warranty of merchantability or 
   fitness for a particular purpose.   
    
   see the gnu general public license at <http://www.gnu.org/licenses/gpl.html> 
   for more details. 
*/

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>                /* must have rw locking on the config_nodes ... */

#include <util.h>
#include <hash.h>
#include <set.h>
#include <path_fmt.h>
#include <thread_pool.h>
#include <stringlist.h>

#include <ecl_grid.h>

#include <job_queue.h>
#include <lsf_driver.h>
#include <local_driver.h>
#include <rsh_driver.h>
#include <ext_joblist.h>

#include <config.h>

#include <enkf_config_node.h>
#include <enkf_types.h>
#include <field_config.h>
#include <gen_data_config.h>
#include <surface_config.h>
#include <meas_data.h>
#include <enkf_types.h>
#include <sched_file.h>
#include <gen_kw_config.h>
#include <summary.h>
#include <summary_config.h>
#include <gen_data.h>
#include <gen_kw_config.h>
#include <gen_data_config.h>
#include <ensemble_config.h>
#include <gen_data_config.h>
#include <field_trans.h>
#include <subst_func.h>
#include <enkf_obs.h>
#include <ecl_config.h>
#include "config_keys.h"
#include "enkf_defaults.h"


struct ensemble_config_struct {
  pthread_mutex_t          mutex;
  char                   * gen_kw_format_string;   /* format string used when creating gen_kw search/replace strings. */
  hash_type              * config_nodes;           /* a hash of enkf_config_node instances - which again conatin pointers to e.g. field_config objects.  */
  field_trans_table_type * field_trans_table;      /* a table of the transformations which are available to apply on fields. */
  const ecl_sum_type     * refcase;                /* a ecl_sum reference instance - can be null (not owned by the ensemble
                                                      config). is only used to check that summary keys are valid when adding. */
};



/**
   setting the format string used to 'mangle' the string in the gen_kw
   template files. consider the following example:

      parameter file
      --------------
      multpv   logunif  0.0001 0.10


      template file
      -------------
      box
         1  10  1 10  1 5 /

      multpv  500*__multpv__

   here the parameter file defines a parameter named 'multpv', and the
   template file uses the marker string '__multpv__' which should be
   replaced with a numerical value. for the current example the
   gen_kw_format_string should have the value '__%s__'.

   there are no rules for the format string, but it _must_ contain a
   '%s' placeholder which will be replaced with the parameter name
   (this is not checked for). the function call creating a search
   string from a parameter name is:

      tagged_string = util_alloc_sprintf( gen_kw_format_string , parameter_name );

*/

void ensemble_config_set_gen_kw_format( ensemble_config_type * ensemble_config , const char * gen_kw_format_string) {
  if (!util_string_equal( gen_kw_format_string , ensemble_config->gen_kw_format_string)) {
    stringlist_type * gen_kw_keys = ensemble_config_alloc_keylist_from_impl_type( ensemble_config , GEN_KW );
    int i;
    ensemble_config->gen_kw_format_string = util_realloc_string_copy( ensemble_config->gen_kw_format_string , gen_kw_format_string );
    for (i=0; i < stringlist_get_size( gen_kw_keys ); i++) {
      enkf_config_node_type * config_node = ensemble_config_get_node( ensemble_config , stringlist_iget( gen_kw_keys , i ));
      gen_kw_config_update_tag_format( enkf_config_node_get_ref( config_node ) , gen_kw_format_string );
    }
    stringlist_free( gen_kw_keys );
  }
}


const char * ensemble_config_get_gen_kw_format( const ensemble_config_type * ensemble_config ) {
  return ensemble_config->gen_kw_format_string;
}


void ensemble_config_set_refcase( ensemble_config_type * ensemble_config , const ecl_sum_type * refcase) {
  ensemble_config->refcase = refcase;
}
                                                                                                           



ensemble_config_type * ensemble_config_alloc_empty( ) {

  ensemble_config_type * ensemble_config = util_malloc(sizeof * ensemble_config );
  ensemble_config->config_nodes          = hash_alloc();
  ensemble_config->refcase               = NULL;
  ensemble_config->gen_kw_format_string  = util_alloc_string_copy( DEFAULT_GEN_KW_TAG_FORMAT );
  pthread_mutex_init( &ensemble_config->mutex , NULL);
  
  return ensemble_config;
}



ert_impl_type ensemble_config_impl_type(const ensemble_config_type *ensemble_config, const char * ecl_kw_name) {
  ert_impl_type impl_type = INVALID;

  if (hash_has_key(ensemble_config->config_nodes , ecl_kw_name)) {
    enkf_config_node_type * node = hash_get(ensemble_config->config_nodes , ecl_kw_name);
    impl_type = enkf_config_node_get_impl_type(node);
  } else
    util_abort("%s: internal error: asked for implementation type of unknown node:%s \n",__func__ , ecl_kw_name);

  return impl_type;
}


enkf_var_type ensemble_config_var_type(const ensemble_config_type *ensemble_config, const char * ecl_kw_name) {
  enkf_var_type var_type = INVALID_VAR;

  if (hash_has_key(ensemble_config->config_nodes , ecl_kw_name)) {
    enkf_config_node_type * node = hash_get(ensemble_config->config_nodes , ecl_kw_name);
    var_type = enkf_config_node_get_var_type(node);
  } else
    util_abort("%s: internal error: asked for implementation type of unknown node:%s \n",__func__ , ecl_kw_name);

  return var_type;
}





void ensemble_config_free(ensemble_config_type * ensemble_config) {
  hash_free( ensemble_config->config_nodes );
  field_trans_table_free( ensemble_config->field_trans_table );
  free( ensemble_config->gen_kw_format_string );
  free( ensemble_config );
}






bool ensemble_config_has_key(const ensemble_config_type * ensemble_config , const char * key) {
  return hash_has_key( ensemble_config->config_nodes , key);
} 



enkf_config_node_type * ensemble_config_get_node(const ensemble_config_type * ensemble_config, const char * key) {
  if (hash_has_key(ensemble_config->config_nodes , key)) {
    enkf_config_node_type * node = hash_get(ensemble_config->config_nodes , key);
    return node;
  } else {
    util_abort("%s: ens node:\"%s\" does not exist \n",__func__ , key);
    return NULL; /* compiler shut up */
  }
}


/** 
    this will remove the config node indexed by key, it will use the
    function hash_safe_del(), which is thread_safe, and will not fail
    if the node has already been removed from the hash. 

    however - it is extremely important to ensure that all storage
    nodes (which point to the config nodes) have been deleted before
    calling this function. that is only assured by using
    enkf_main_del_node().
*/


void ensemble_config_del_node(ensemble_config_type * ensemble_config, const char * key) {
  hash_safe_del(ensemble_config->config_nodes , key);
}


void ensemble_config_add_node__( ensemble_config_type * ensemble_config , enkf_config_node_type * node) {
  
  const char * key = enkf_config_node_get_key( node );
  if (ensemble_config_has_key(ensemble_config , key)) 
    util_abort("%s: a configuration object:%s has already been added - aborting \n",__func__ , key);
  hash_insert_hash_owned_ref(ensemble_config->config_nodes , key , node , enkf_config_node_free__);
}


enkf_config_node_type *  ensemble_config_add_node(ensemble_config_type * ensemble_config , 
                                                  const char    * key              , 
                                                  enkf_var_type  enkf_type         , 
                                                  ert_impl_type  impl_type         ,
                                                  const char   * enkf_outfile      , /* written by enkf and read by forward model */
                                                  const char   * enkf_infile       , /* written by forward model and read by enkf */ 
                                                  void         * data ) {

  if (ensemble_config_has_key(ensemble_config , key)) 
      util_abort("%s: a configuration object:%s has already been added - aborting \n",__func__ , key);
  
  {
    const char * init_file = "hhh";
    
    enkf_config_node_type * node = enkf_config_node_alloc(enkf_type , impl_type , key , init_file , enkf_outfile , enkf_infile , data );
    hash_insert_hash_owned_ref(ensemble_config->config_nodes , key , node , enkf_config_node_free__);
    return node;
  }
}



/**
   this is called by the enkf_state function while loading results,
   that code is run in parallell by many threads.
*/
void ensemble_config_ensure_static_key(ensemble_config_type * ensemble_config , const char * kw ) {
  pthread_mutex_lock( &ensemble_config->mutex );
  {
    if (!ensemble_config_has_key(ensemble_config , kw)) 
      ensemble_config_add_node(ensemble_config , kw , STATIC_STATE , STATIC , NULL , NULL , NULL);
  }
  pthread_mutex_unlock( &ensemble_config->mutex );
}


void ensemble_config_add_obs_key(ensemble_config_type * ensemble_config , const char * key, const char * obs_key) {
  enkf_config_node_type * config_node = hash_get(ensemble_config->config_nodes , key);
  enkf_config_node_add_obs_key(config_node , obs_key);
}


void ensemble_config_clear_obs_keys(ensemble_config_type * ensemble_config) {
  hash_iter_type * iter = hash_iter_alloc( ensemble_config->config_nodes );
  while (!hash_iter_is_complete( iter )) {
    enkf_config_node_type * config_node = hash_iter_get_next_value( iter );
    enkf_config_node_clear_obs_keys( config_node );
  }
  hash_iter_free( iter );
}



void ensemble_config_add_config_items(config_type * config) {
  config_schema_item_type * item;

  /** 
      the two fault types are just added to the config object only to
      be able to print suitable messages before exiting.
  */
      
  item = config_add_schema_item(config , "HAVANA_FAULT" , false  );
  config_schema_item_set_argc_minmax(item , 2 , 2 ,  0 , NULL );

  item = config_add_schema_item(config , "MULTFLT" , false  );
  config_schema_item_set_argc_minmax(item , 3 , 3 ,  3 , (const config_item_types [3]) { CONFIG_STRING , CONFIG_STRING , CONFIG_EXISTING_PATH});


  /*****************************************************************/
  
  item = config_add_schema_item(config , GEN_KW_KEY , false  );
  config_schema_item_set_argc_minmax(item , 4 , 6 ,  6 , (const config_item_types [6]) { CONFIG_STRING , CONFIG_EXISTING_PATH , CONFIG_STRING , CONFIG_EXISTING_PATH , CONFIG_STRING , CONFIG_STRING});
  
  item = config_add_key_value( config , GEN_KW_TAG_FORMAT_KEY , false , CONFIG_STRING);
  
  item = config_add_schema_item(config , SCHEDULE_PREDICTION_FILE_KEY , false  );
  /* scedhule_prediction_file   filename  <parameters:> <init_files:> */
  config_schema_item_set_argc_minmax(item , 1 , 3 ,  3 , (const config_item_types [3]) { CONFIG_EXISTING_PATH , CONFIG_STRING , CONFIG_STRING});

  item = config_add_schema_item(config , GEN_PARAM_KEY , false  );
  config_schema_item_set_argc_minmax(item , 5 , CONFIG_DEFAULT_ARG_MAX ,  0 , NULL);
  
  item = config_add_schema_item(config , GEN_DATA_KEY , false  );
  config_schema_item_set_argc_minmax(item , 1 , CONFIG_DEFAULT_ARG_MAX ,  0 , NULL);

  item = config_add_schema_item(config , SUMMARY_KEY , false  );   /* can have several summary keys on each line. */
  config_schema_item_set_argc_minmax(item , 1 , CONFIG_DEFAULT_ARG_MAX ,  0 , NULL);

  item = config_add_schema_item(config , CONTAINER_KEY , false  );   /* can have several summary keys on each line. */
  config_schema_item_set_argc_minmax(item , 2 , CONFIG_DEFAULT_ARG_MAX ,  0 , NULL);
  
  item = config_add_schema_item( config , SURFACE_KEY , false  );
  config_schema_item_set_argc_minmax(item , 4 , 5 ,  0 , NULL);
  /* 
     the way config info is entered for fields is unfortunate because
     it is difficult/impossible to let the config system handle run
     time validation of the input.
  */
  
  item = config_add_schema_item(config , FIELD_KEY , false  );
  config_schema_item_set_argc_minmax(item , 2 , CONFIG_DEFAULT_ARG_MAX ,  0 , NULL);
  config_schema_item_add_required_children(item , GRID_KEY);   /* if you are using a field - you must have a grid. */
}


void ensemble_config_init_GEN_PARAM( ensemble_config_type * ensemble_config , const config_type * config ) {
  /* gen_param  - should be unified with the gen_data*/
  const config_content_item_type * item = config_get_content_item( config , GEN_PARAM_KEY );
  if (item != NULL) {
    int i;
    for (i=0; i < config_content_item_get_size(config); i++) {
      const config_content_node_type * node = config_content_item_iget_node( item , i );
      const char * key                      = config_content_node_iget( node , 0 );
      const char * ecl_file                 = config_content_node_iget( node , 1 );
      enkf_config_node_type * config_node   = ensemble_config_add_gen_data( ensemble_config , key );
      {
        hash_type * options = hash_alloc();
        

        gen_data_file_format_type input_format  = gen_data_config_check_format( hash_safe_get( options , INPUT_FORMAT_KEY));
        gen_data_file_format_type output_format = gen_data_config_check_format( hash_safe_get( options , OUTPUT_FORMAT_KEY));
        const char * init_file_fmt              = hash_safe_get( options , INIT_FILES_KEY);
        const char * template                   = hash_safe_get( options , TEMPLATE_KEY);
        const char * key                        = hash_safe_get( options , KEY_KEY);
        const char * result_file                = hash_safe_get( options , RESULT_FILE_KEY);
        const char * min_std_file               = hash_safe_get( options , MIN_STD_KEY);
        
        enkf_config_node_update_gen_data( config_node , input_format , output_format , init_file_fmt , template , key , ecl_file , result_file , min_std_file);
        {
          const gen_data_config_type * gen_data_config = enkf_config_node_get_ref( config_node );
          if (!gen_data_config_is_valid( gen_data_config ))
            util_abort("%s: sorry the gen_param key:%s is not valid \n",__func__ , key);
        }
        hash_free( options );
      }
    }
  }
}



/**
   observe that if the user has not given a refcase with the refcase
   key the refcase pointer will be NULL. in that case it will be
   impossible to use wildcards when expanding summary variables.
*/

void ensemble_config_init(ensemble_config_type * ensemble_config , const config_type * config , ecl_grid_type * grid, const ecl_sum_type * refcase) {
  int i;
  ensemble_config->field_trans_table     = field_trans_table_alloc();    
  ensemble_config_set_refcase( ensemble_config , refcase );

  if (config_item_set( config , GEN_KW_TAG_FORMAT_KEY))
    ensemble_config_set_gen_kw_format( ensemble_config , config_iget( config , GEN_KW_TAG_FORMAT_KEY , 0 , 0 ));
  
  ensemble_config_init_GEN_PARAM( ensemble_config , config );
  
  /* gen_data */
  for (i=0; i < config_get_occurences(config , GEN_DATA_KEY); i++) {
    const stringlist_type * tokens = config_iget_stringlist_ref(config , GEN_DATA_KEY , i);
    const char * key                          = stringlist_iget(tokens , 0);
    enkf_config_node_type * config_node       = ensemble_config_add_gen_data( ensemble_config , key );
    {
      hash_type * options = hash_alloc_from_options( tokens );
      gen_data_file_format_type input_format  = gen_data_config_check_format( hash_safe_get( options , INPUT_FORMAT_KEY));
      gen_data_file_format_type output_format = gen_data_config_check_format( hash_safe_get( options , OUTPUT_FORMAT_KEY));
      const char * init_file_fmt              = hash_safe_get( options , INIT_FILES_KEY);
      const char * template                   = hash_safe_get( options , TEMPLATE_KEY);
      const char * key                        = hash_safe_get( options , KEY_KEY);
      const char * ecl_file                   = hash_safe_get( options , ECL_FILE_KEY);
      const char * result_file                = hash_safe_get( options , RESULT_FILE_KEY);
      const char * min_std_file               = hash_safe_get( options , MIN_STD_KEY);

      enkf_config_node_update_gen_data( config_node , 
                                        input_format , 
                                        output_format , 
                                        init_file_fmt , 
                                        template , 
                                        key , 
                                        ecl_file , 
                                        result_file , 
                                        min_std_file);
      {
        const gen_data_config_type * gen_data_config = enkf_config_node_get_ref( config_node );
        if (!gen_data_config_is_valid( gen_data_config ))
          util_abort("%s: sorry the gen_data key:%s is not valid \n",__func__ , key);
      }
      hash_free( options );
    }
  }


  /* surface */
  {
    for (i=0; i < config_get_occurences( config , SURFACE_KEY ); i++) {
      const stringlist_type * tokens   = config_iget_stringlist_ref(config , SURFACE_KEY , i);
      const char * key           = stringlist_iget(tokens , 0);
      {
        hash_type * options = hash_alloc_from_options( tokens );  /* INIT_FILE:<init_files>  OUTPUT_FILE:<outfile>  BASE_SURFACE:<base_file> */

        const char * init_file_fmt = hash_safe_get( options , INIT_FILES_KEY );
        const char * output_file   = hash_safe_get( options , OUTPUT_FILE_KEY);
        const char * base_surface  = hash_safe_get( options , BASE_SURFACE_KEY);
        const char * min_std_file  = hash_safe_get( options , MIN_STD_KEY);

        if ((init_file_fmt == NULL) || (output_file == NULL) || (base_surface == NULL)) {
          fprintf(stderr,"** error: when entering a surface you must provide arguments:\n");
          fprintf(stderr,"**   %s:/path/to/input/files%%d  \n",INIT_FILES_KEY);
          fprintf(stderr,"**   %s:name_of_output_file\n", OUTPUT_FILE_KEY);
          fprintf(stderr,"**   %s:base_surface_file\n",base_surface);
          exit(1);
        }

        {
          enkf_config_node_type * config_node = ensemble_config_add_surface( ensemble_config , key );
          enkf_config_node_update_surface( config_node , base_surface , init_file_fmt , output_file , min_std_file );
        }
        
        hash_free( options );
      }
    }
  }




  /* field */
  {
    for (i=0; i < config_get_occurences(config , FIELD_KEY); i++) {
      const stringlist_type * tokens            = config_iget_stringlist_ref(config , FIELD_KEY , i);
      const char *  key                   = stringlist_iget(tokens , 0);
      const char *  var_type_string       = stringlist_iget(tokens , 1);
      enkf_config_node_type * config_node = ensemble_config_add_field( ensemble_config , key , grid );
      
      {
        hash_type * options = hash_alloc_from_options( tokens );
        
        int    truncation = TRUNCATE_NONE;
        double value_min  = -1;
        double value_max  = -1;
        
        if (hash_has_key( options , MIN_KEY)) {
          truncation |= TRUNCATE_MIN;
          value_min   = atof(hash_get( options , MIN_KEY));
        }

        if (hash_has_key( options , MAX_KEY)) {
          truncation |= TRUNCATE_MAX;
          value_max   = atof(hash_get( options , MAX_KEY));
        }
        
        
        if (strcmp(var_type_string , DYNAMIC_KEY) == 0) 
          enkf_config_node_update_state_field( config_node , truncation , value_min , value_max );
        else if (strcmp(var_type_string , PARAMETER_KEY) == 0) {
          const char *  ecl_file          = stringlist_iget(tokens , 2);
          const char *  init_file_fmt     = hash_safe_get( options , INIT_FILES_KEY );
          const char *  init_transform    = hash_safe_get( options , INIT_TRANSFORM_KEY );
          const char *  output_transform  = hash_safe_get( options , OUTPUT_TRANSFORM_KEY );
          const char *  min_std_file      = hash_safe_get( options , MIN_STD_KEY );
          
          enkf_config_node_update_parameter_field( config_node, 
                                                   ecl_file          , 
                                                   init_file_fmt     , 
                                                   min_std_file      , 
                                                   truncation        , 
                                                   value_min         , 
                                                   value_max         ,    
                                                   init_transform    , 
                                                   output_transform   );
        } else if (strcmp(var_type_string , GENERAL_KEY) == 0) {
          const char *  ecl_file          = stringlist_iget(tokens , 2);
          const char *  enkf_infile       = stringlist_iget(tokens , 3);
          const char *  init_file_fmt     = hash_safe_get( options , INIT_FILES_KEY );
          const char *  init_transform    = hash_safe_get( options , INIT_TRANSFORM_KEY );
          const char *  output_transform  = hash_safe_get( options , OUTPUT_TRANSFORM_KEY );
          const char *  input_transform   = hash_safe_get( options , INPUT_TRANSFORM_KEY );
          const char *  min_std_file      = hash_safe_get( options , MIN_STD_KEY );
          

          enkf_config_node_update_general_field( config_node,
                                                 ecl_file , 
                                                 enkf_infile , 
                                                 init_file_fmt , 
                                                 min_std_file , 
                                                 truncation , value_min , value_max , 
                                                 init_transform , 
                                                 input_transform , 
                                                 output_transform);

          
        } else 
          util_abort("%s: field type: %s is not recognized\n",__func__ , var_type_string);
        
        hash_free( options );
      }
    }
  }

  /* gen_kw */
  for (i=0; i < config_get_occurences(config , GEN_KW_KEY); i++) {
    stringlist_type * tokens = stringlist_alloc_deep_copy( config_iget_stringlist_ref(config , GEN_KW_KEY , i) );
    char * key            = stringlist_iget_copy(tokens , 0);
    char * template_file  = stringlist_iget_copy(tokens , 1);
    char * enkf_outfile   = stringlist_iget_copy(tokens , 2);
    char * parameter_file = stringlist_iget_copy(tokens , 3);
    stringlist_idel( tokens , 0);
    stringlist_idel( tokens , 0);
    stringlist_idel( tokens , 0);
    stringlist_idel( tokens , 0);

    {
      hash_type * opt_hash                = hash_alloc_from_options( tokens );
      enkf_config_node_type * config_node = ensemble_config_add_gen_kw( ensemble_config , key );
      enkf_config_node_update_gen_kw( config_node , 
                                      enkf_outfile , 
                                      template_file , 
                                      parameter_file , 
                                      hash_safe_get( opt_hash , MIN_STD_KEY ) , 
                                      hash_safe_get( opt_hash , INIT_FILES_KEY));
      hash_free( opt_hash );
    }

    free(key);
    free(template_file);
    free(enkf_outfile);
    free(parameter_file);
    stringlist_free( tokens );
  }


  /* summary */
  {
    stringlist_type * keys = stringlist_alloc_new ( );
    
    for (i=0; i < config_get_occurences(config , SUMMARY_KEY ); i++) {
      int j,k;
      const stringlist_type * summary_kw_list = config_iget_stringlist_ref(config , SUMMARY_KEY , i);
      for (j= 0; j < stringlist_get_size( summary_kw_list ); j++) {
        const char * key = stringlist_iget( summary_kw_list , j); 
        
        if (util_string_has_wildcard( key )) {
          if (ensemble_config->refcase != NULL) {
            ecl_sum_select_matching_general_var_list( ensemble_config->refcase , key , keys );   /* expanding the wildcard notatition with help of the refcase. */
            for (k=0; k < stringlist_get_size( keys ); k++) 
              ensemble_config_add_summary(ensemble_config , stringlist_iget(keys , k) , LOAD_FAIL_SILENT );
          } else
            util_exit("error: when using summary wildcards like: \"%s\" you must supply a valid refcase.\n",key);
        } else 
          ensemble_config_add_summary(ensemble_config , key , LOAD_FAIL_SILENT);
      }
    }
    
    stringlist_free( keys );
  }

  /* Containers - this must come last, to ensure that the other nodes have been added. */
  {
    for (i=0; i < config_get_occurences(config , CONTAINER_KEY ); i++) {
      const stringlist_type * container_kw_list = config_iget_stringlist_ref(config , CONTAINER_KEY , i);
      const char * container_key = stringlist_iget( container_kw_list , 0 );
      enkf_config_node_type * container_node = ensemble_config_add_container( ensemble_config , container_key );
      
      for (int j= 1; j < stringlist_get_size( container_kw_list ); j++) {
        const char * child_key = stringlist_iget( container_kw_list , j); 
        enkf_config_node_update_container( container_node , ensemble_config_get_node( ensemble_config , child_key ));
      }
    }
  }

  /*****************************************************************/
}

/**
   this function takes a string like this: "pressure:1,4,7" - it
   splits the string on ":" and tries to lookup a config object with
   that key. for the general string a:b:c:d it will try consecutively
   the keys: a, a:b, a:b:c, a:b:c:d. if a config object is found it is
   returned, otherwise NULL is returned.

   the last argument is the pointer to a string which will be updated
   with the node-spesific part of the full key. so for instance with
   the example "pressure:1,4,7", the index_key will contain
   "1,4,7". if the full full_key is used to find an object index_key
   will be NULL, that also applies if no object is found.
*/

   

const enkf_config_node_type * ensemble_config_user_get_node(const ensemble_config_type * config , const char  * full_key, char ** index_key ) {
  const enkf_config_node_type * node = NULL;
  char ** key_list;
  int     keys;
  int     key_length = 1;
  int offset;
  
  *index_key = NULL;
  util_split_string(full_key , USER_KEY_JOIN_STRING , &keys , &key_list);
  while (node == NULL && key_length <= keys) {
    char * current_key = util_alloc_joined_string( (const char **) key_list , key_length , USER_KEY_JOIN_STRING );
    if (ensemble_config_has_key(config , current_key))
      node = ensemble_config_get_node(config , current_key);
    else
      key_length++;
    offset = strlen( current_key );
    free( current_key );
  }
  if (node != NULL) {
    if (offset < strlen( full_key ))
      *index_key = util_alloc_string_copy(&full_key[offset+1]);
  }
  
  util_free_stringlist(key_list , keys);
  return node;
}



stringlist_type * ensemble_config_alloc_keylist(const ensemble_config_type * config) {
  return hash_alloc_stringlist( config->config_nodes );
}


/**
   observe that var_type here is an integer - naturally written as a
   sum of enkf_var_type values:

     ensemble_config_alloc_keylist_from_var_type( config , parameter + dynamic_state);

*/
   
stringlist_type * ensemble_config_alloc_keylist_from_var_type(const ensemble_config_type * config , int var_mask) {
  stringlist_type * key_list = stringlist_alloc_new();
  hash_iter_type * iter = hash_iter_alloc(config->config_nodes);

  while (!hash_iter_is_complete( iter )) {
    const char * key       = hash_iter_get_next_key(iter);
    enkf_var_type var_type = enkf_config_node_get_var_type( hash_get(config->config_nodes , key));
    
    if (var_type & var_mask)
      stringlist_append_copy( key_list , key );
  }
  hash_iter_free(iter);

  return key_list;
}



stringlist_type * ensemble_config_alloc_keylist_from_impl_type(const ensemble_config_type * config , ert_impl_type impl_type) {
  stringlist_type * key_list = stringlist_alloc_new();
  hash_iter_type * iter = hash_iter_alloc(config->config_nodes);
  while (!hash_iter_is_complete( iter )) {
    const char * key = hash_iter_get_next_key(iter);
    if (enkf_config_node_get_impl_type( hash_get(config->config_nodes , key)) == impl_type)
      stringlist_append_copy( key_list , key );

  }
  hash_iter_free(iter);
  return key_list;
}




void ensemble_config_init_internalization( ensemble_config_type * config ) {
  hash_iter_type * iter = hash_iter_alloc(config->config_nodes);
  const char * key = hash_iter_get_next_key(iter);
  while (key != NULL) {
    enkf_config_node_init_internalization( hash_get(config->config_nodes , key));
    key = hash_iter_get_next_key(iter);
  }
  hash_iter_free(iter);
}




/**
   this function will look up the user_key in the ensemble_config. if
   the corresponding config_node can not be found 0 will be returned,
   otherwise enkf_config_node functions will be invoked.
*/


int ensemble_config_get_observations( const ensemble_config_type * config , enkf_obs_type * enkf_obs , const char * user_key , int obs_count , time_t * obs_time , double * y , double * std) {
  int num_obs = 0;
  char * index_key;
  const enkf_config_node_type * config_node = ensemble_config_user_get_node( config , user_key , &index_key);
  if (config_node != NULL) {
    num_obs = enkf_config_node_load_obs( config_node , enkf_obs , index_key , obs_count , obs_time , y , std);
    util_safe_free( index_key );
  } 
  return num_obs;
}


/*****************************************************************/


/* 
   the ensemble_config_add_xxx() functions below will create a new xxx
   instance and add it to the ensemble_config; the return value from
   the functions is the newly created config_node instances.

   the newly created enkf_config_node instances are __not__ fully
   initialized, and a subsequent call to enkf_config_node_update_xxx()
   is essential for proper operation.
*/

enkf_config_node_type * ensemble_config_add_field( ensemble_config_type * config , const char * key , ecl_grid_type * ecl_grid ) {
  enkf_config_node_type * config_node = enkf_config_node_new_field( key , ecl_grid , config->field_trans_table );
  ensemble_config_add_node__( config , config_node );
  return config_node;
}


enkf_config_node_type * ensemble_config_add_gen_kw( ensemble_config_type * config , const char * key ) {
  enkf_config_node_type * config_node = enkf_config_node_new_gen_kw( key , config->gen_kw_format_string );
  ensemble_config_add_node__( config , config_node );
  return config_node;
}


enkf_config_node_type * ensemble_config_add_gen_data( ensemble_config_type * config , const char * key ) {
  enkf_config_node_type * config_node = enkf_config_node_new_gen_data( key );
  ensemble_config_add_node__( config , config_node );
  return config_node;
}


/**
   this function ensures that object contains a node with 'key' and
   type == summary.
   
   if the @refcase pointer is different from NULL the key will be
   validated. keys which do not exist in the refcase will be ignored,
   a warning will be printed on stderr and the function will return
   NULL.
*/

enkf_config_node_type * ensemble_config_add_summary(ensemble_config_type * ensemble_config , const char * key , load_fail_type load_fail) {
  enkf_config_node_type * config_node = NULL;

  if (hash_has_key(ensemble_config->config_nodes, key)) {
    config_node = hash_get(ensemble_config->config_nodes, key);
    if (enkf_config_node_get_impl_type( config_node ) != SUMMARY)
      util_abort("%s: ensemble key:%s already exists - but it is not of summary type\n",__func__ , key);
    {
      summary_config_type * summary_config = enkf_config_node_get_ref( config_node );
      summary_config_update_load_fail_mode( summary_config , load_fail );
    }
  } else {
    if ((ensemble_config->refcase == NULL) || (ecl_sum_has_general_var( ensemble_config->refcase , key ))) {
      config_node = enkf_config_node_alloc_summary( key , load_fail);
      ensemble_config_add_node__(ensemble_config , config_node );
    } else
      fprintf(stderr,"** warning: the refcase:%s does not contain the summary key:\"%s\" - will be ignored.\n", ecl_sum_get_case( ensemble_config->refcase ) , key);
  }

  return config_node;
}


enkf_config_node_type * ensemble_config_add_surface( ensemble_config_type * ensemble_config , const char * key ) {
  enkf_config_node_type * config_node = enkf_config_node_new_surface( key );
  ensemble_config_add_node__( ensemble_config , config_node );
  return config_node;
}


/*
  If key == NULL the function will create a random key.
*/
enkf_config_node_type * ensemble_config_add_container( ensemble_config_type * ensemble_config , const char * key) {
  char * local_key = key;
  bool  random_key = false;
  if (key == NULL) {
    local_key = util_calloc( 11 , sizeof * local_key  );
    sprintf(local_key , "%ld" , random() % 10000000 ); 
    random_key = true;
  } 

  {
    enkf_config_node_type * config_node = enkf_config_node_new_container( local_key );
    ensemble_config_add_node__( ensemble_config , config_node );
    if (random_key)
      free( local_key );
    return config_node;
  }
}




/*****************************************************************/

void ensemble_config_fprintf_config( ensemble_config_type * ensemble_config , FILE * stream ) {
  fprintf( stream , CONFIG_COMMENTLINE_FORMAT );
  fprintf( stream , CONFIG_COMMENT_FORMAT , "Here comes configuration information about the uncertain parameters and response variables in use.");

  fprintf( stream , CONFIG_KEY_FORMAT      , GEN_KW_TAG_FORMAT_KEY );
  fprintf( stream , CONFIG_ENDVALUE_FORMAT , ensemble_config->gen_kw_format_string);


  /* Writing GEN_KW nodes. */
  {
    stringlist_type * gen_kw_keys = ensemble_config_alloc_keylist_from_impl_type( ensemble_config , GEN_KW );
    stringlist_sort( gen_kw_keys , NULL );
    for (int i=0; i < stringlist_get_size( gen_kw_keys ); i++) {
      const enkf_config_node_type * config_node = ensemble_config_get_node( ensemble_config , stringlist_iget( gen_kw_keys , i));
      enkf_config_node_fprintf_config( config_node , stream );
    }
    if (stringlist_get_size( gen_kw_keys ) > 0)
      fprintf(stream , "\n");
    stringlist_free( gen_kw_keys );
  }

  
  /* Writing FIELD nodes. */
  {
    stringlist_type * field_keys = ensemble_config_alloc_keylist_from_impl_type( ensemble_config , FIELD );
    stringlist_sort( field_keys , NULL );
    for (int i=0; i < stringlist_get_size( field_keys ); i++) {
      const enkf_config_node_type * config_node = ensemble_config_get_node( ensemble_config , stringlist_iget( field_keys , i));
      enkf_config_node_fprintf_config( config_node , stream );
    }
    if (stringlist_get_size( field_keys ) > 0)
      fprintf(stream , "\n");
    stringlist_free( field_keys );
  }


  /* Writing SUMMARY nodes. */
  {
    stringlist_type * summary_keys = ensemble_config_alloc_keylist_from_impl_type( ensemble_config , SUMMARY );
    stringlist_sort( summary_keys , NULL );
    for (int i=0; i < stringlist_get_size( summary_keys ); i++) {
      if (i == 0)
        fprintf(stream , CONFIG_KEY_FORMAT , SUMMARY_KEY);
      else if ((i % 8) == 0) {
        fprintf(stream , "\n");
        fprintf(stream , CONFIG_KEY_FORMAT , SUMMARY_KEY);
      }
      fprintf(stream , CONFIG_SHORT_VALUE_FORMAT , stringlist_iget( summary_keys , i ));
    }
    fprintf(stream , "\n");
    stringlist_free( summary_keys );
  }
  fprintf(stream , "\n");
  

  /* Writing GEN_DATA nodes. */
  {
    stringlist_type * gen_data_keys = ensemble_config_alloc_keylist_from_impl_type( ensemble_config , GEN_DATA );
    stringlist_sort( gen_data_keys , NULL );
    for (int i=0; i < stringlist_get_size( gen_data_keys ); i++) {
      const enkf_config_node_type * config_node = ensemble_config_get_node( ensemble_config , stringlist_iget( gen_data_keys , i));
      enkf_config_node_fprintf_config( config_node , stream );
    }
    stringlist_free( gen_data_keys );
  }
  fprintf(stream , "\n\n");
}
