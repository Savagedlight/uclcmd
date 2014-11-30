/*-
 * Copyright (c) 2014 Allan Jude <allanjude@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <errno.h>
#include <getopt.h>
#include <stdio.h>

#include <ucl.h>

#ifndef __DECONST
#define __DECONST(type, var)    ((type)(uintptr_t)(const void *)(var))
#endif

/*
 * XXX: TODO:
 * Error checking on strtoul etc
 * Does ucl_object_insert_key_common need to respect NO_IMPLICIT_ARRAY
 */

static int show_keys = 0, show_raw = 0, nonewline = 0, mode = 0, debug = 0;
static bool firstline = true;
static int output_type = 254;
static ucl_object_t *root_obj = NULL;
static ucl_object_t *set_obj = NULL;
struct ucl_parser *parser = NULL;
struct ucl_parser *setparser = NULL;
static char *dot = ".";
static char *sepchar = ".";


void usage();
void get_mode(char *requested_node);
int process_get_command(const ucl_object_t *obj, char *nodepath,
    const char *command_str, char *remaining_commands, int recurse);
int set_mode(char *requested_node, char *data);
int merge_mode(char *requested_node, char *data);
ucl_object_t* parse_input(struct ucl_parser *parser, FILE *source);
ucl_object_t* parse_string(struct ucl_parser *parser, char *data);
ucl_object_t* parse_file(struct ucl_parser *parser, const char *filename);
ucl_object_t* get_parent(char *selected_node);
ucl_object_t* get_object(char *selected_node);
void output_key(const ucl_object_t *obj, char *nodepath, const char *inkey);
void output_chunk(const ucl_object_t *obj, char *nodepath, const char *inkey);
void replace_sep(char *key, char *oldsep, char *newsep);
void cleanup();
void ucl_obj_dump (const ucl_object_t *obj, unsigned int shift);

/*
 * This application provides a shell scripting friendly interface for reading
 * and writing UCL config files, using libUCL.
 */
int
main(int argc, char *argv[])
{
    const char *filename = NULL;
    int ret = 0, k = 0, ch;
    bool success = false;

    if (argc == 0) {
	usage();
    }

    /* Initialize parser */
    parser = ucl_parser_new(UCL_PARSER_KEY_LOWERCASE |
        UCL_PARSER_NO_IMPLICIT_ARRAYS);

    /*	options	descriptor */
    static struct option longopts[] = {
	{ "cjson",	no_argument,            &output_type,
	    UCL_EMIT_JSON_COMPACT },
	{ "debug",      optional_argument,      NULL,       	'd' },
	{ "file",       required_argument,      NULL,       	'f' },
	{ "get",        no_argument,            &mode,      	0 },
	{ "json",       no_argument,            &output_type,
	    UCL_EMIT_JSON },
	{ "keys",       no_argument,            &show_keys, 	1 },
	{ "merge",    	no_argument,            &mode,  	2 },
	{ "mix",    	no_argument,            &mode,  	3 },
	{ "nonewline",  no_argument,            &nonewline,  	1 },
	{ "noquote",    no_argument,            &show_raw,  	1 },
	{ "remove",     no_argument,            &mode,      	4 },
	{ "set",        no_argument,            &mode,      	1 },
	{ "shellvars",  no_argument,            NULL,      	'v' },
	{ "ucl",        no_argument,            &output_type,
	    UCL_EMIT_CONFIG },
	{ "yaml",       no_argument,            &output_type,	UCL_EMIT_YAML },
	{ NULL,         0,                      NULL,       	0 }
    };

    while ((ch = getopt_long(argc, argv, "cdf:gijkmnqrsuvy", longopts, NULL)) != -1) {
	switch (ch) {
	case 'c':
	    output_type = UCL_EMIT_JSON_COMPACT;
	    break;
	case 'd':
	    if (optarg != NULL) {
		debug = strtol(optarg, NULL, 0);
	    } else {
		debug = 1;
	    }
	    break;
	case 'f':
	    filename = optarg;
	    if (strcmp(optarg, "-") == 0) {
		/* Input from STDIN */
		root_obj = parse_input(parser, stdin);
	    } else {
		root_obj = parse_file(parser, filename);
	    }
	    break;
	case 'i':
	    mode = 3;
	    break;
	case 'j':
	    output_type = UCL_EMIT_JSON;
	    break;
	case 'g':
	    mode = 0;
	    break;
	case 'k':
	    show_keys = 1;
	    break;
	case 'n':
	    nonewline = 1;
	    break;
	case 'm':
	    mode = 2;
	    break;
	case 'q':
	    show_raw = 1;
	    break;
	case 'r':
	    mode = 3;
	    break;
	case 's':
	    mode = 1;
	    break;
	case 'u':
	    output_type = UCL_EMIT_CONFIG;
	    break;
	case 'v':
	    sepchar = "_";
	    break;
	case 'y':
	    output_type = UCL_EMIT_YAML;
	    break;
	case 0:
	    break;
	default:
	    fprintf(stderr, "Error: Unexpected option: %i\n", ch);
	    usage();
	    break;
	}
    }
    argc -= optind;
    argv += optind;

    if (argc == 0) {
	usage();
    }

    switch (mode) {
    case 0: /* Get Mode */
	if (argc == 0) {
	    fprintf(stderr, "Error: No get performed.\n");
	    cleanup();
	    exit(4);
	}

	if (filename == NULL) {
	    root_obj = parse_input(parser, stdin);
	}

	for (k = 0; k < argc; k++) {
	    get_mode(argv[k]);
	}
	break;
    case 1: /* Set Mode */
	if (argc == 0) {
	    fprintf(stderr, "Error: No set performed.\n");
	    cleanup();
	    exit(4);
	}

	if (filename == NULL) {
	    root_obj = parse_input(parser, stdin);
	}

	if (argc > 1) {
	    success = set_mode(argv[0], argv[1]);
	} else {
	    success = set_mode(argv[0], NULL);
	}

	if (success) {
	    get_mode(dot);
	} else {
	    fprintf(stderr, "Error: Failed to apply the set operation.\n");
	    ret = 1;
	}
	break;
    case 2: /* merge */
	if (argc == 0) {
	    fprintf(stderr, "Error: No merge performed.\n");
	    cleanup();
	    exit(4);
	}

	if (filename == NULL) {
	    root_obj = parse_input(parser, stdin);
	}

	if (argc > 1) {
	    success = merge_mode(argv[0], argv[1]);
	} else {
	    success = merge_mode(argv[0], NULL);
	}

	if (success) {
	    get_mode(dot);
	} else {
	    fprintf(stderr, "Error: Failed to apply the merge operation.\n");
	    ret = 1;
	}
	break;
    case 4: /* remove */
	/* Parse the original UCL */
	if (argc == 0) {
	    fprintf(stderr, "Error: No remove performed.\n");
	    cleanup();
	    exit(4);
	}

	if (filename == NULL) {
	    root_obj = parse_input(parser, stdin);
	}

	ret = ucl_object_delete_key(root_obj, argv[0]);
	get_mode(dot);

	break;
    }

    cleanup();

    if (nonewline) {
	printf("\n");
    }
    return ret;
}

void
usage()
{
    fprintf(stderr, "%s\n",
"Usage: uclcmd [-cdijkmnqruvy] [-f filename] --get variable\n"
"       uclcmd [-cdijkmnqruvy] [-f filename] --set variable UCL\n"
"       uclcmd [-cdijkmnqruvy] [-f filename] --merge variable UCL\n"
"       uclcmd [-cdijkmnqruvy] [-f filename] --mix variable UCL\n"
"\n"
"OPTIONS:\n"
"       -c --cjson      output compacted JSON\n"
"       -d --debug      enable verbose debugging output\n"
"       -f --file       path to a file to read or write\n"
"       -i --mix        merge-and-replace provided UCL into the indicated key\n"
"       -j --json       output pretty JSON\n"
"       -k --keys       show key=value rather than just the value\n"
"       -m --merge      merge the provided UCL into the indicated key\n"
"       -n --nonewline  separate output with spaces rather than newlines\n"
"       -q --noquote    do not enclose strings in quotes\n"
"       -r --remove     delete the indicated key\n"
"       -g --get        read a variable\n"
"       -s --set        write a block of UCL\n"
"       -u --ucl        output universal config language\n"
"       -v --shellvars  keys are output with underscores instead of dots\n"
"       -y --yaml       output YAML\n"
"       variable        The key of the variable to read, in object notation\n"
"       UCL             A block of UCL to be written to the specified variable\n"
"\n"
"EXAMPLES:\n"
"       uclcmd --file vmconfig --get .name\n"
"           \"value\"\n"
"\n"
"       uclcmd --file vmconfig --keys --noquotes --get array.1.name\n"
"           array.1.name=value\n"
"\n"
"       uclcmd --file vmconfig --keys --shellvars --get array.1.name\n"
"           array_1_name=\"value\"\n"
"\n");
    exit(1);
}

ucl_object_t*
parse_input(struct ucl_parser *parser, FILE *source)
{
    unsigned char inbuf[8192];
    int r = 0;
    ucl_object_t *obj = NULL;

    while (!feof(source) && r < (int)sizeof(inbuf)) {
	r += fread(inbuf + r, 1, sizeof(inbuf) - r, source);
    }
    ucl_parser_add_chunk(parser, inbuf, r);
    fclose(source);

    if (ucl_parser_get_error(parser)) {
	fprintf(stderr, "Error occured: %s\n",
	    ucl_parser_get_error(parser));
	cleanup();
	exit(2);
    }

    obj = ucl_parser_get_object(parser);
    if (ucl_parser_get_error(parser)) {
	fprintf(stderr, "Error: Parse Error occured: %s\n",
	    ucl_parser_get_error(parser));
	cleanup();
	exit(3);
    }

    return obj;
}

ucl_object_t*
parse_string(struct ucl_parser *parser, char *data)
{
    ucl_object_t *obj = NULL;
    int success = 0;

    success = ucl_parser_add_string(parser, data, 0);
    if (success == false) {
	/* There must be a better way to detect a string */
	ucl_parser_clear_error(parser);
	success = true;
	obj = ucl_object_fromstring_common(data, 0, UCL_STRING_PARSE);
    } else {
	obj = ucl_parser_get_object(parser);
    }

    if (ucl_parser_get_error(parser)) {
	fprintf(stderr, "Error: Parse Error occured: %s\n",
	    ucl_parser_get_error(parser));
	cleanup();
	exit(3);
    }

    return obj;
}

ucl_object_t*
parse_file(struct ucl_parser *parser, const char *filename)
{
    ucl_object_t *obj = NULL;

    ucl_parser_add_file(parser, filename);

    if (ucl_parser_get_error(parser)) {
	fprintf(stderr, "Error occured: %s\n",
	    ucl_parser_get_error(parser));
	cleanup();
	exit(2);
    }

    obj = ucl_parser_get_object(parser);
    if (ucl_parser_get_error(parser)) {
	fprintf(stderr, "Error: Parse Error occured: %s\n",
	    ucl_parser_get_error(parser));
	cleanup();
	exit(3);
    }

    return obj;
}

ucl_object_t*
get_parent(char *selected_node)
{
    char *dst_key = selected_node;
    char *dst_prefix = strdup(dst_key);
    char *dst_frag = strrchr(dst_prefix, '.');
    ucl_object_t *parent_obj = NULL;

    if (dst_frag == NULL || strlen(dst_frag) == 0) {
	dst_frag = dst_key;
	parent_obj = root_obj;
    } else {
	/*
	 * dst_frag is a pointer to the last period in dst_prefix, write a
	 * null byte to split the two strings, and then advance the pointer
	 * to the first byte after the period
	 */
	dst_frag[0] = '\0';
	dst_frag++;
	parent_obj = __DECONST(ucl_object_t *,
	    ucl_lookup_path(root_obj, dst_prefix));
    }

    free(dst_prefix);

    if (parent_obj == NULL) {
	return NULL;
    }

    return parent_obj;
}

ucl_object_t*
get_object(char *selected_node)
{
    char *dst_key = selected_node;
    char *dst_prefix = strdup(dst_key);
    char *dst_frag = strrchr(dst_prefix, '.');
    ucl_object_t *parent_obj = NULL;
    ucl_object_t *selected_obj = NULL;

    if (dst_frag == NULL || strlen(dst_frag) == 0) {
	dst_frag = dst_key;
	parent_obj = root_obj;
	if (parent_obj->type == UCL_ARRAY) {
	    /* XXX: Error detection in strtoul needed here */
	    selected_obj = __DECONST(ucl_object_t *,
		ucl_array_find_index(parent_obj, strtoul(dst_frag, NULL, 10)));
	} else {
	    selected_obj = __DECONST(ucl_object_t *,
		ucl_object_find_key(parent_obj, dst_frag));
	}
    } else {
	/*
	 * dst_frag is a pointer to the last period in dst_prefix, write a
	 * null byte to split the two strings, and then advance the pointer
	 * to the first byte after the period
	 */
	dst_frag[0] = '\0';
	dst_frag++;
	parent_obj = __DECONST(ucl_object_t *,
	    ucl_lookup_path(root_obj, dst_prefix));
	if (parent_obj == NULL) {
	    free(dst_prefix);
	    return NULL;
	}
	if (parent_obj->type == UCL_ARRAY) {
	    selected_obj = __DECONST(ucl_object_t *,
		ucl_array_find_index(parent_obj, strtoul(dst_frag, NULL, 10)));
	} else {
	    selected_obj = __DECONST(ucl_object_t *,
		ucl_object_find_key(parent_obj, dst_frag));
	}
    }
    if (selected_obj == NULL) {
	selected_obj = parent_obj;
    }

    if (debug > 0) {
	fprintf(stderr, "selecting key: %s\n", dst_key);
	fprintf(stderr, "selected sub-key: %s\n", dst_frag);
    }

    free(dst_prefix);

    return selected_obj;
}

void
get_mode(char *requested_node)
{
    const ucl_object_t *found_object;
    char *cmd = requested_node;
    char *node_name = strsep(&cmd, "|");
    char *command_str = strsep(&cmd, "|");
    char *nodepath = NULL;
    int command_count = 0, i;

    asprintf(&nodepath, "");
    found_object = root_obj;

    if (strlen(node_name) == 0) {
	/* Requested root node */
	if (debug > 0) {
	    fprintf(stderr, "DEBUG: Using root node\n");
	}
	found_object = root_obj;
    } else if (strcmp(node_name, ".") == 0) {
	if (debug > 0) {
	    fprintf(stderr, "DEBUG: Using root node\n");
	}
	found_object = root_obj;
    } else {
	if (strncmp(node_name, ".", 1) == 0) {
	    /* Removing leading dot */
	    node_name++;
	}
	/* Search for selected node */
	if (debug > 0) {
	    fprintf(stderr, "DEBUG: Searching node %s\n", node_name);
	}
	found_object = ucl_lookup_path(found_object, node_name);
	free(nodepath);
	asprintf(&nodepath, "%s", node_name);
    }

    while (command_str != NULL) {
	if (debug > 0) {
	    fprintf(stderr, "DEBUG: Performing \"%s\" command on \"%s\"...\n",
		command_str, node_name);
	}
	int done = process_get_command(found_object, nodepath, command_str,
	    cmd, 1);
	if (debug >= 2) {
	    fprintf(stderr, "DEBUG: Finished process, did: %i commands\n",
		done);
	}

	for (i = 0; i < done; i++) {
	    if (debug >= 2) {
		fprintf(stderr, "DEBUG: Removing command: %s\n", command_str);
	    }
	    command_str = strsep(&cmd, "|");
	}
	if (debug >= 2) {
	    fprintf(stderr, "DEBUG: Remaining command: %s\n", command_str);
	}
	command_count += done;
    }

    if (debug >= 2) {
	fprintf(stderr, "DEBUG: Ending get_mode with command_count=%i\n",
	    command_count);
    }
    if (command_count == 0) {
	output_chunk(found_object, nodepath, "");
    }
    free(nodepath);
}

int
process_get_command(const ucl_object_t *obj, char *nodepath,
    const char *command_str, char *remaining_commands, int recurse)
{
    ucl_object_iter_t it = NULL, it2 = NULL;
    const ucl_object_t *cur, *cur2;
    int command_count = 0, loopcount = 0;
    int recurse_level = recurse;
    int arrindex = 0;

    if (debug >= 2) {
	fprintf(stderr, "DEBUG: Got command: %s - next command: %s\n",
	    command_str, remaining_commands);
    }
    if (strcmp(command_str, "length") == 0) {
	/* Return the number of items in an array */
	if (firstline == false) {
	    printf(" ");
	}
	if (obj == NULL) {
	    if (show_keys == 1)
		printf("(null)=");
	    printf("0");
	} else {
	    if (show_keys == 1)
		printf("%s", nodepath);
	    printf("%u", obj->len);
	}
	if (nonewline) {
	    firstline = false;
	} else {
	    printf("\n");
	}
    } else if (strcmp(command_str, "type") == 0) {
	/* Return the type of the current object */
	if (firstline == false) {
	    printf(" ");
	}
	if (obj == NULL) {
	    if (show_keys == 1)
		printf("(null)=");
	    printf("null");
	} else {
	    if (show_keys == 1)
		printf("%s=", nodepath);
	    switch(obj->type) {
	    case UCL_OBJECT:
		printf("object");
		break;
	    case UCL_ARRAY:
		printf("array");
		break;
	    case UCL_INT:
		printf("int");
		break;
	    case UCL_FLOAT:
		printf("float");
		break;
	    case UCL_STRING:
		printf("string");
		break;
	    case UCL_BOOLEAN:
		printf("boolean");
		break;
	    case UCL_TIME:
		printf("time");
		break;
	    case UCL_USERDATA:
		printf("userdata");
		break;
	    case UCL_NULL:
		printf("null");
		break;
	    default:
		printf("unknown");
		break;
	    }
	}
	if (nonewline) {
	    firstline = false;
	} else {
	    printf("\n");
	}
    } else if (strcmp(command_str, "keys") == 0) {
	if (obj != NULL) {
	    /* Return the keys of the current object */
	    while ((cur = ucl_iterate_object(obj, &it, true))) {
		if (firstline == false) {
		    printf(" ");
		}
		printf("%s", ucl_object_key(cur));
		if (nonewline) {
		    firstline = false;
		} else {
		    printf("\n");
		}
		loopcount++;
	    }
	}
	if (loopcount == 0 && debug > 0) {
	    fprintf(stderr, "DEBUG: Found 0 keys\n");
	}
    } else if (strcmp(command_str, "values") == 0) {
	if (obj != NULL) {
	    /* Return the values of the current object */
	    while ((cur = ucl_iterate_object(obj, &it, true))) {
		char *newkey = NULL;
		if (cur == NULL) {
		    continue;
		}
		if (obj->type == UCL_ARRAY) {
		    asprintf(&newkey, "%s%i", sepchar, arrindex);
		    arrindex++;
		} else {
		    asprintf(&newkey, "%s%s", sepchar, ucl_object_key(cur));
		}
		output_key(cur, nodepath, newkey);
		loopcount++;
		free(newkey);
	    }
	}
	if (loopcount == 0 && debug > 0) {
	    fprintf(stderr, "DEBUG: Found 0 values\n");
	}
    } else if (strcmp(command_str, "iterate") == 0) {
	if (remaining_commands == NULL) {
	    it = NULL;
	    char blankkey = '\0';
	    while ((cur = ucl_iterate_object(obj, &it, false))) {
		output_chunk(cur, nodepath, &blankkey);
		loopcount++;
	    }
	} else if (obj != NULL) {
	    /* Return the values of the current object */
	    char *rcmds = strdup(remaining_commands);
	    char *next_command = strsep(&rcmds, "|");
	    if (next_command != NULL) {
		it = NULL;
		while ((cur = ucl_iterate_object(obj, &it, false))) {
		    recurse_level = process_get_command(cur, nodepath,
			next_command, rcmds, recurse + 1);
		}
		loopcount++;
	    }
	}
	if (loopcount == 0 && debug > 0) {
	    fprintf(stderr, "DEBUG: Found 0 objects to each over\n");
	}
    } else if (strcmp(command_str, "each") == 0) {
	if (remaining_commands == NULL) {
	    it = NULL;
	    while ((cur = ucl_iterate_object(obj, &it, true))) {
		char *newkey = NULL;
		if (obj->type == UCL_ARRAY) {
		    asprintf(&newkey, "%s%i", sepchar, arrindex);
		    arrindex++;
		} else {
		    asprintf(&newkey, "%s%s", sepchar, ucl_object_key(cur));
		}
		if (cur->next != 0 && cur->type != UCL_ARRAY) {
		    it2 = NULL;
		    while ((cur2 = ucl_iterate_object(cur, &it2, false))) {
			output_chunk(cur2, nodepath, newkey);
		    }
		} else {
		    output_chunk(cur, nodepath, newkey);
		}
		loopcount++;
		free(newkey);
	    }
	} else if (obj != NULL) {
	    /* Return the values of the current object */
	    char *rcmds = strdup(remaining_commands);
	    char *next_command = strsep(&rcmds, "|");
	    if (next_command != NULL) {
		it = NULL;
		while ((cur = ucl_iterate_object(obj, &it, true))) {
		    char *newnodepath = NULL;
		    if (obj->type == UCL_ARRAY) {
			asprintf(&newnodepath, "%s%s%i", nodepath, sepchar,
			    arrindex);
			arrindex++;
		    } else {
			asprintf(&newnodepath, "%s%s%s", nodepath, sepchar,
			    ucl_object_key(cur));
		    }
		    if (cur->next != 0 && cur->type != UCL_ARRAY) {
			it2 = NULL;
			while ((cur2 = ucl_iterate_object(cur, &it2, false))) {
			    recurse_level = process_get_command(cur2,
				newnodepath, next_command, rcmds, recurse + 1);
			}
		    } else {
			recurse_level = process_get_command(cur, newnodepath,
			    next_command, rcmds, recurse + 1);
		    }
		    loopcount++;
		    free(newnodepath);
		}
	    }
	}
	if (loopcount == 0 && debug > 0) {
	    fprintf(stderr, "DEBUG: Found 0 objects to each over\n");
	}
    } else if (strncmp(command_str, ".", 1) == 0) {
	/* User has provided an identifier after the commands */
	/* Search for selected node */
	if (debug > 0) {
	    fprintf(stderr, "DEBUG: Searching for subnode %s\n", command_str);
	}
	cur = ucl_lookup_path(obj, command_str);
	/* If this is the last thing on the stack, output */
	if (remaining_commands == NULL) {
	    /* Would also check cur==null here, but that breaks |keys */
	    output_key(cur, nodepath, command_str);
	} else {
	    /* Return the values of the current object */
	    char *rcmds = strdup(remaining_commands);
	    char *next_command = strsep(&rcmds, "|");
	    if (next_command != NULL) {
		char *newnodepath = NULL;
		if (obj->type == UCL_ARRAY) {
		    asprintf(&newnodepath, "%s%s%i", nodepath, sepchar,
			arrindex);
		    arrindex++;
		} else {
		    asprintf(&newnodepath, "%s%s%s", nodepath, sepchar,
			ucl_object_key(cur));
		}
		if (debug > 2) {
		    fprintf(stderr, "DEBUG: Calling recurse with %s.%s on %s\n",
			newnodepath, next_command,
			ucl_object_emit(cur, UCL_EMIT_CONFIG));
		}
		recurse_level = process_get_command(cur, newnodepath,
		    next_command, rcmds, recurse + 1);
		free(newnodepath);
	    }
	}
    } else {
	/* Not a valid command */
	fprintf(stderr, "Error: invalid command %s\n", command_str);
	exit(1);
    }
    command_count++;
    if (debug >= 3) {
	fprintf(stderr, "DEBUG: Returning p_g_c with c_count=%i rlevel=%i\n",
	    command_count, recurse_level);
    }
    return recurse_level;
}

int
set_mode(char *destination_node, char *data)
{
    ucl_object_t *dst_obj = NULL;
    ucl_object_t *sub_obj = NULL;
    ucl_object_t *old_obj = NULL;
    int success = 0;

    setparser = ucl_parser_new(UCL_PARSER_KEY_LOWERCASE |
	UCL_PARSER_NO_IMPLICIT_ARRAYS);

    /* Lookup the destination to write to */
    dst_obj = get_parent(destination_node);
    sub_obj = get_object(destination_node);

    if (data == NULL || strcmp(data, "-") == 0) {
	/* get UCL to add from stdin */
	set_obj = parse_input(setparser, stdin);
    } else {
	/* User provided data inline */
	if (sub_obj->type != UCL_OBJECT && sub_obj->type != UCL_ARRAY) {
	    /* Destination is a scalar etc */
	    set_obj = ucl_object_fromstring_common(data, 0,
		UCL_STRING_PARSE);
	} else {
	    /* Destination is an Object or Array */
	    set_obj = parse_string(setparser, data);
	}
    }

    if (debug > 0) {
	fprintf(stderr, "Inserting key %s to root: %s\n",
	    ucl_object_key(sub_obj), ucl_object_key(dst_obj));
    }

    /* Replace it in the object here */
    if (dst_obj->type == UCL_ARRAY) {
	char *dst_frag = strrchr(destination_node, '.');

	dst_frag++;
	old_obj = ucl_array_replace_index(dst_obj, set_obj, strtoul(dst_frag,
	    NULL, 0));
	success = false;
	if (old_obj != NULL) {
	    ucl_object_unref(old_obj);
	    success = true;
	}
    } else {
	success = ucl_object_replace_key(dst_obj, set_obj,
	    ucl_object_key(sub_obj), 0, true);
	set_obj = NULL;
    }

    return success;
}

int
merge_mode(char *destination_node, char *data)
{
    ucl_object_t *dst_obj = NULL;
    ucl_object_t *sub_obj = NULL;
    ucl_object_t *old_obj = NULL;
    int success = 0;

    setparser = ucl_parser_new(UCL_PARSER_KEY_LOWERCASE |
	UCL_PARSER_NO_IMPLICIT_ARRAYS);

    /* Lookup the destination to write to */
    dst_obj = get_parent(destination_node);
    sub_obj = get_object(destination_node);

    if (data == NULL || strcmp(data, "-") == 0) {
	/* get UCL to add from stdin */
	set_obj = parse_input(setparser, stdin);
    } else {
	/* User provided data inline */
	if (sub_obj->type != UCL_OBJECT && sub_obj->type != UCL_ARRAY) {
	    /* Destination is a scalar etc */
	    set_obj = ucl_object_fromstring_common(data, 0,
		UCL_STRING_PARSE);
	} else {
	    /* Destination is an Object or Array */
	    set_obj = parse_string(setparser, data);
	}
    }

    if (debug > 0) {
	fprintf(stderr, "Merging key %s to root: %s\n",
	    ucl_object_key(sub_obj), ucl_object_key(dst_obj));
    }

    /* Add it to the object here */
    if (sub_obj->type == UCL_ARRAY && set_obj->type == UCL_ARRAY) {
	success = ucl_array_merge(sub_obj, set_obj, false);
    } else if (sub_obj->type == UCL_OBJECT && set_obj->type == UCL_OBJECT) {
	success = ucl_object_merge(sub_obj, set_obj, false);
    } else if (sub_obj->type == UCL_ARRAY) {
	success = ucl_array_append(sub_obj, ucl_object_ref(set_obj));
    } else if (sub_obj->type != UCL_OBJECT && sub_obj->type != UCL_ARRAY &&
	    set_obj->type != UCL_OBJECT && set_obj->type != UCL_ARRAY) {
	/* Create an explicit array */
	old_obj = ucl_object_typed_new(UCL_ARRAY);
	/*
	 * Reference and Append the original scalar
	 * The additional reference is required because the old object will be
	 * unreferenced as part of the ucl_object_replace_key operation
	 */
	ucl_array_append(old_obj, ucl_object_ref(sub_obj));
	/* Reference and Append the new scalar (unref in cleanup()) */
	ucl_array_append(old_obj, ucl_object_ref(set_obj));
	/* Replace the old object with the newly created one */
	success = ucl_object_replace_key(dst_obj, old_obj,
	    ucl_object_key(sub_obj), 0, true);
    } else if (dst_obj->type == UCL_OBJECT && set_obj->type != UCL_OBJECT
	    && set_obj->type != UCL_ARRAY) {
	printf("Not implemented yet\n");
    } else {
	success = ucl_object_insert_key_merged(dst_obj, set_obj,
	    ucl_object_key(sub_obj), 0, true);
    }

    return success;
}

void
output_key(const ucl_object_t *obj, char *nodepath, const char *inkey)
{
    char *key = strdup(inkey);

    replace_sep(nodepath, ".", sepchar);
    replace_sep(key, ".", sepchar);
    if (firstline == false) {
	printf(" ");
    }
    if (obj == NULL) {
	if (show_keys == 1) {
	    printf("%s%s=", nodepath, key);
	}
	printf("null");
	if (nonewline) {
	    firstline = false;
	} else {
	    printf("\n");
	}
	return;
    }
    switch (obj->type) {
    case UCL_OBJECT:
	if (debug >= 3) {
	    fprintf(stderr, "DEBUG: key=%s\nlen=%u\ntype=UCL_OBJECT\n"
		"value={object}\n", obj->key, obj->len);
	}
	if (show_keys == 1)
	    printf("%s%s=", nodepath, key);
	printf("{object}");
	break;
    case UCL_ARRAY:
	if (debug >= 3) {
	    fprintf(stderr, "DEBUG: key=%s\nlen=%u\ntype=UCL_ARRAY\n"
		"value=[array]\n", obj->key, obj->len);
	}
	if (show_keys == 1)
	    printf("%s%s=", nodepath, key);
	printf("[array]");
	break;
    case UCL_INT:
	if (debug >= 3) {
	    fprintf(stderr, "DEBUG: key=%s\nlen=%u\ntype=UCL_INT\nvalue=%jd\n",
		obj->key, obj->len, (intmax_t)ucl_object_toint(obj));
	}
	if (show_keys == 1)
	    printf("%s%s=", nodepath, key);
	printf("%jd", (intmax_t)ucl_object_toint(obj));
	break;
    case UCL_FLOAT:
	if (debug >= 3) {
	    fprintf(stderr, "DEBUG: key=%s\nlen=%u\ntype=UCL_FLOAT\nvalue=%f\n",
		obj->key, obj->len, ucl_object_todouble(obj));
	}
	if (show_keys == 1)
	    printf("%s%s=", nodepath, key);
	printf("%f", ucl_object_todouble(obj));
	break;
    case UCL_STRING:
	if (debug >= 3) {
	    fprintf(stderr, "DEBUG: key=%s\nlen=%u\ntype=UCL_STRING\n"
		"value=\"%s\"\n", obj->key, obj->len, ucl_object_tostring(obj));
	}
	if (show_keys == 1)
	    printf("%s%s=", nodepath, key);
	if (show_raw == 1)
	    printf("%s", ucl_object_tostring(obj));
	else
	    printf("\"%s\"", ucl_object_tostring(obj));
	break;
    case UCL_BOOLEAN:
	if (debug >= 3) {
	    fprintf(stderr, "DEBUG: key=%s\nlen=%u\ntype=UCL_BOOLEAN\n"
		"value=%s\n", obj->key, obj->len,
		ucl_object_tostring_forced(obj));
	}
	if (show_keys == 1)
	    printf("%s%s=", nodepath, key);
	printf("%s", ucl_object_tostring_forced(obj));
	break;
    case UCL_TIME:
	if (debug >= 3) {
	    fprintf(stderr, "DEBUG: key=%s\nlen=%u\ntype=UCL_TIME\nvalue=%f\n",
		obj->key, obj->len, ucl_object_todouble(obj));
	}
	if (show_keys == 1)
	    printf("%s%s=", nodepath, key);
	printf("%f", ucl_object_todouble(obj));
	break;
    case UCL_USERDATA:
	if (debug >= 3) {
	    fprintf(stderr, "DEBUG: key=%s\nlen=%u\ntype=UCL_USERDATA\n"
		"value=%p\n", obj->key, obj->len, obj->value.ud);
	}
	if (show_keys == 1)
	    printf("%s%s=", nodepath, key);
	printf("{userdata}");
	break;
    default:
	if (debug >= 3) {
	    printf("error=Object of unknown type\n");
	    fprintf(stderr, "DEBUG: key=%s\nlen=%u\ntype=UCL_ERROR\n"
		"value=null\n", obj->key, obj->len);
	}
	break;
    }
    if (nonewline) {
	firstline = false;
    } else {
	printf("\n");
    }

    free(key);
}

void
output_chunk(const ucl_object_t *obj, char *nodepath, const char *inkey)
{
    unsigned char *result = NULL;
    char *key = strdup(inkey);

    replace_sep(nodepath, ".", sepchar);
    replace_sep(key, ".", sepchar);

    switch (output_type) {
    case 254: /* Text */
	output_key(obj, nodepath, key);
	break;
    case UCL_EMIT_CONFIG: /* UCL */
	result = ucl_object_emit(obj, output_type);
	if (nonewline) {
	    fprintf(stderr, "WARN: UCL output cannot be 'nonewline'd\n");
	}
	if (show_keys == 1)
	    printf("%s%s=", nodepath, key);
	printf("%s", result);
	free(result);
	if (nonewline) {
	    firstline = false;
	} else {
	    printf("\n");
	}
	break;
    case UCL_EMIT_JSON: /* JSON */
	result = ucl_object_emit(obj, output_type);
	if (nonewline) {
	    fprintf(stderr,
		"WARN: non-compact JSON output cannot be 'nonewline'd\n");
	}
	if (show_keys == 1)
	    printf("%s%s=", nodepath, key);
	printf("%s", result);
	free(result);
	if (nonewline) {
	    firstline = false;
	} else {
	    printf("\n");
	}
	break;
    case UCL_EMIT_JSON_COMPACT: /* Compact JSON */
	result = ucl_object_emit(obj, output_type);
	if (show_keys == 1)
	    printf("%s%s=", nodepath, key);
	printf("%s", result);
	free(result);
	if (nonewline) {
	    firstline = false;
	} else {
	    printf("\n");
	}
	break;
    case UCL_EMIT_YAML: /* YAML */
	result = ucl_object_emit(obj, output_type);
	if (nonewline) {
	    fprintf(stderr, "WARN: YAML output cannot be 'nonewline'd\n");
	}
	if (show_keys == 1)
	    printf("%s%s=", nodepath, key);
	printf("%s", result);
	free(result);
	if (nonewline) {
	    firstline = false;
	} else {
	    printf("\n");
	}
	break;
    default:
	fprintf(stderr, "Error: Invalid output mode: %i\n",
	    output_type);
	break;
    }

    free(key);
}

void
replace_sep(char *key, char *oldsep, char *newsep)
{
    int idx = 0;
    if (oldsep == newsep) return;
    while (key[idx] != '\0') {
	if (key[idx] == oldsep[0]) {
	    key[idx] = newsep[0];
	}
	idx++;
    }
}

void
cleanup()
{
    if (parser != NULL) {
	ucl_parser_free(parser);
    }
    if (setparser != NULL) {
	ucl_parser_free(setparser);
    }
    if (root_obj != NULL) {
	ucl_object_unref(root_obj);
    }
    if (set_obj != NULL) {
	ucl_object_unref(set_obj);
    }
}

void
ucl_obj_dump (const ucl_object_t *obj, unsigned int shift)
{
	int num = shift * 4 + 5;
	char *pre = (char *) malloc (num * sizeof(char));
	const ucl_object_t *cur, *tmp;
	ucl_object_iter_t it = NULL, it_obj = NULL;

	pre[--num] = 0x00;
	while (num--)
		pre[num] = 0x20;

	tmp = obj;

	while ((obj = ucl_iterate_object (tmp, &it, false))) {
		printf ("%sucl object address: %p\n", pre + 4, obj);
		if (obj->key != NULL) {
			printf ("%skey: \"%s\"\n", pre, ucl_object_key (obj));
		}
		printf ("%sref: %u\n", pre, obj->ref);
		printf ("%slen: %u\n", pre, obj->len);
		printf ("%sprev: %p\n", pre, obj->prev);
		printf ("%snext: %p\n", pre, obj->next);
		if (obj->type == UCL_OBJECT) {
			printf ("%stype: UCL_OBJECT\n", pre);
			printf ("%svalue: %p\n", pre, obj->value.ov);
			it_obj = NULL;
			while ((cur = ucl_iterate_object (obj, &it_obj, true))) {
				ucl_obj_dump (cur, shift + 2);
			}
		}
		else if (obj->type == UCL_ARRAY) {
			printf ("%stype: UCL_ARRAY\n", pre);
			printf ("%svalue: %p\n", pre, obj->value.av);
			ucl_obj_dump (obj->value.av, shift + 2);
		}
		else if (obj->type == UCL_INT) {
			printf ("%stype: UCL_INT\n", pre);
			printf ("%svalue: %jd\n", pre, (intmax_t)ucl_object_toint (obj));
		}
		else if (obj->type == UCL_FLOAT) {
			printf ("%stype: UCL_FLOAT\n", pre);
			printf ("%svalue: %f\n", pre, ucl_object_todouble (obj));
		}
		else if (obj->type == UCL_STRING) {
			printf ("%stype: UCL_STRING\n", pre);
			printf ("%svalue: \"%s\"\n", pre, ucl_object_tostring (obj));
		}
		else if (obj->type == UCL_BOOLEAN) {
			printf ("%stype: UCL_BOOLEAN\n", pre);
			printf ("%svalue: %s\n", pre, ucl_object_tostring_forced (obj));
		}
		else if (obj->type == UCL_TIME) {
			printf ("%stype: UCL_TIME\n", pre);
			printf ("%svalue: %f\n", pre, ucl_object_todouble (obj));
		}
		else if (obj->type == UCL_USERDATA) {
			printf ("%stype: UCL_USERDATA\n", pre);
			printf ("%svalue: %p\n", pre, obj->value.ud);
		}
	}

	free (pre);
}
