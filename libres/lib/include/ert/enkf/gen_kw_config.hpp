/*
   Copyright (C) 2011  Equinor ASA, Norway.

   The file 'gen_kw_config.h' is part of ERT - Ensemble based Reservoir Tool.

   ERT is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   ERT is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.

   See the GNU General Public License at <http://www.gnu.org/licenses/gpl.html>
   for more details.
*/

#ifndef ERT_GEN_KW_CONFIG_H
#define ERT_GEN_KW_CONFIG_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdbool.h>

#include <ert/util/stringlist.h>
#include <ert/util/util.h>

#include <ert/enkf/enkf_util.hpp>
#include <ert/enkf/enkf_macros.hpp>
#include <ert/enkf/gen_kw_common.hpp>

const char *gen_kw_config_get_tag_fmt(const gen_kw_config_type *config);
bool gen_kw_config_is_valid(const gen_kw_config_type *config);
void gen_kw_config_fprintf_config(const gen_kw_config_type *config,
                                  const char *outfile, const char *min_std_file,
                                  FILE *stream);
void gen_kw_config_set_parameter_file(gen_kw_config_type *config,
                                      const char *parameter_file);
PY_USED const char *
gen_kw_config_get_parameter_file(const gen_kw_config_type *config);
const char *gen_kw_config_get_key(const gen_kw_config_type *config);
const char *gen_kw_config_get_template_file(const gen_kw_config_type *);
void gen_kw_config_free(gen_kw_config_type *);
double gen_kw_config_transform(const gen_kw_config_type *, int index, double x);
bool gen_kw_config_should_use_log_scale(const gen_kw_config_type *config,
                                        int index);
int gen_kw_config_get_data_size(const gen_kw_config_type *);
const char *gen_kw_config_iget_name(const gen_kw_config_type *, int);
const char *gen_kw_config_get_tagged_name(const gen_kw_config_type *, int);
stringlist_type *
gen_kw_config_alloc_name_list(const gen_kw_config_type *config);
int gen_kw_config_get_index(const gen_kw_config_type *, const char *);
void gen_kw_config_set_template_file(gen_kw_config_type *config,
                                     const char *template_file);
gen_kw_config_type *gen_kw_config_alloc_empty(const char *key,
                                              const char *tag_fmt);
void gen_kw_config_update(gen_kw_config_type *config, const char *template_file,
                          const char *parameter_file);
void gen_kw_config_update_tag_format(gen_kw_config_type *config,
                                     const char *tag_format);
PY_USED const char *
gen_kw_config_iget_function_type(const gen_kw_config_type *config, int index);
double_vector_type *
gen_kw_config_iget_function_parameter_values(const gen_kw_config_type *config,
                                             int index);
stringlist_type *
gen_kw_config_iget_function_parameter_names(const gen_kw_config_type *config,
                                            int index);

UTIL_SAFE_CAST_HEADER_CONST(gen_kw_config);
UTIL_SAFE_CAST_HEADER(gen_kw_config);
VOID_FREE_HEADER(gen_kw_config);
VOID_GET_DATA_SIZE_HEADER(gen_kw);
#ifdef __cplusplus
}
#endif
#endif
