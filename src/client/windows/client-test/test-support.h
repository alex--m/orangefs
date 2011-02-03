/* Copyright (C) 2011 Omnibond, LLC 
   Client testing -- declarations */

#ifndef __TESTS_H
#define __TESTS_H

typedef struct
{
    const char *root_dir;
	char *tab_file;
	int fs;
    int report_flags;
    FILE *freport;
} global_options;

/* constants */
#define CODE_FATAL    0x11111111L

#define RESULT_SUCCESS   0
#define RESULT_FAILURE   1

#define OPER_EQUAL          0
#define OPER_NOTEQUAL       1
#define OPER_LESS           2
#define OPER_GREATER        3
#define OPER_LESSOREQUAL    4
#define OPER_GREATEROREQUAL 5

#define REPORT_CONSOLE    1
#define REPORT_FILE       2

/* functions */
char *quickcat(const char *str1, const char *str2);

char *randdir(const char *root);

char *randfile(const char *root);

void report_error(global_options *options,
                  const char *msg);

void report_result(global_options *options,
                   const char *test_name,
                   int expected_result,
                   int expected_code,
                   int code_operation,
                   int actual_code);

#endif