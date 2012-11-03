exec_program( svnversion ${PROJECT_SOURCE_DIR} OUTPUT_VARIABLE SVN_VERSION)
exec_program( date OUTPUT_VARIABLE COMPILE_TIME_STAMP)
file( WRITE ${CMAKE_CURRENT_BINARY_DIR}/build_timestamp.h "#define SVN_VERSION  \"${SVN_VERSION}\"\n#define COMPILE_TIME_STAMP   \"${COMPILE_TIME_STAMP}\"\n" )

