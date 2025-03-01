/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Fluent Bit
 *  ==========
 *  Copyright (C) 2019-2021 The Fluent Bit Authors
 *  Copyright (C) 2015-2018 Treasure Data Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>

#include <monkey/mk_core.h>
#include <fluent-bit/flb_compat.h>
#include <fluent-bit/flb_info.h>
#include <fluent-bit/flb_dump.h>
#include <fluent-bit/flb_stacktrace.h>
#include <fluent-bit/flb_env.h>
#include <fluent-bit/flb_macros.h>
#include <fluent-bit/flb_utils.h>
#include <fluent-bit/flb_pack.h>
#include <fluent-bit/flb_meta.h>
#include <fluent-bit/flb_config.h>
#include <fluent-bit/flb_version.h>
#include <fluent-bit/flb_error.h>
#include <fluent-bit/flb_custom.h>
#include <fluent-bit/flb_input.h>
#include <fluent-bit/flb_output.h>
#include <fluent-bit/flb_filter.h>
#include <fluent-bit/flb_engine.h>
#include <fluent-bit/flb_str.h>
#include <fluent-bit/flb_slist.h>
#include <fluent-bit/flb_plugin.h>
#include <fluent-bit/flb_parser.h>
#include <fluent-bit/flb_lib.h>
#include <fluent-bit/flb_help.h>
#include <fluent-bit/flb_record_accessor.h>
#include <fluent-bit/flb_ra_key.h>

#ifdef FLB_HAVE_MTRACE
#include <mcheck.h>
#endif

#ifdef FLB_SYSTEM_WINDOWS
extern int win32_main(int, char**);
extern void win32_started(void);
#endif

flb_ctx_t *ctx;
struct flb_config *config;
volatile sig_atomic_t exit_signal = 0;

#ifdef FLB_HAVE_LIBBACKTRACE
struct flb_stacktrace flb_st;
#endif

#define FLB_HELP_TEXT   0
#define FLB_HELP_JSON   1

#define PLUGIN_CUSTOM   0
#define PLUGIN_INPUT    1
#define PLUGIN_OUTPUT   2
#define PLUGIN_FILTER   3

#define print_opt(a, b)      printf("  %-24s%s\n", a, b)
#define print_opt_i(a, b, c) printf("  %-24s%s (default: %i)\n", a, b, c)
#define print_opt_s(a, b, c) printf("  %-24s%s (default: %s)\n", a, b, c)

#define get_key(a, b, c)     mk_rconf_section_get_key(a, b, c)
#define n_get_key(a, b, c)   (intptr_t) get_key(a, b, c)
#define s_get_key(a, b, c)   (char *) get_key(a, b, c)

static void flb_version()
{
    printf("Fluent Bit v%s\n", FLB_VERSION_STR);
    exit(EXIT_SUCCESS);
}

static void flb_banner()
{
    fprintf(stderr, "%sFluent Bit v%s%s\n", ANSI_BOLD, FLB_VERSION_STR,
            ANSI_RESET);
    fprintf(stderr, "* %sCopyright (C) 2019-2021 The Fluent Bit Authors%s\n",
            ANSI_BOLD ANSI_YELLOW, ANSI_RESET);
    fprintf(stderr, "* %sCopyright (C) 2015-2018 Treasure Data%s\n",
            ANSI_BOLD ANSI_YELLOW, ANSI_RESET);
    fprintf(stderr, "* Fluent Bit is a CNCF sub-project under the "
            "umbrella of Fluentd\n");
    fprintf(stderr, "* https://fluentbit.io\n\n");
}

static void flb_help(int rc, struct flb_config *config)
{
    struct mk_list *head;
    struct flb_input_plugin *in;
    struct flb_output_plugin *out;
    struct flb_filter_plugin *filter;

    printf("Usage: fluent-bit [OPTION]\n\n");
    printf("%sAvailable Options%s\n", ANSI_BOLD, ANSI_RESET);
    print_opt("-b  --storage_path=PATH", "specify a storage buffering path");
    print_opt("-c  --config=FILE", "specify an optional configuration file");
#ifdef FLB_HAVE_FORK
    print_opt("-d, --daemon", "run Fluent Bit in background mode");
#endif
    print_opt("-D, --dry-run", "dry run");
    print_opt_i("-f, --flush=SECONDS", "flush timeout in seconds",
                FLB_CONFIG_FLUSH_SECS);
    print_opt("-F  --filter=FILTER", "set a filter");
    print_opt("-i, --input=INPUT", "set an input");
    print_opt("-m, --match=MATCH", "set plugin match, same as '-p match=abc'");
    print_opt("-o, --output=OUTPUT", "set an output");
    print_opt("-p, --prop=\"A=B\"", "set plugin configuration property");
#ifdef FLB_HAVE_PARSER
    print_opt("-R, --parser=FILE", "specify a parser configuration file");
#endif
    print_opt("-e, --plugin=FILE", "load an external plugin (shared lib)");
    print_opt("-l, --log_file=FILE", "write log info to a file");
    print_opt("-t, --tag=TAG", "set plugin tag, same as '-p tag=abc'");
#ifdef FLB_HAVE_STREAM_PROCESSOR
    print_opt("-T, --sp-task=SQL", "define a stream processor task");
#endif
    print_opt("-v, --verbose", "increase logging verbosity (default: info)");
#ifdef FLB_HAVE_TRACE
    print_opt("-vv", "trace mode (available)");
#endif
    print_opt("-w, --workdir", "set the working directory");
#ifdef FLB_HAVE_HTTP_SERVER
    print_opt("-H, --http", "enable monitoring HTTP server");
    print_opt_s("-P, --port", "set HTTP server TCP port",
                FLB_CONFIG_HTTP_PORT);
#endif
    print_opt_i("-s, --coro_stack_size", "set coroutines stack size in bytes",
                config->coro_stack_size);
    print_opt("-q, --quiet", "quiet mode");
    print_opt("-S, --sosreport", "support report for Enterprise customers");
    print_opt("-V, --version", "show version number");
    print_opt("-h, --help", "print this help");

    printf("\n%sInputs%s\n", ANSI_BOLD, ANSI_RESET);

    /* Iterate each supported input */
    mk_list_foreach(head, &config->in_plugins) {
        in = mk_list_entry(head, struct flb_input_plugin, _head);
        if (strcmp(in->name, "lib") == 0 || (in->flags & FLB_INPUT_PRIVATE)) {
            /* useless..., just skip it. */
            continue;
        }
        print_opt(in->name, in->description);
    }

    printf("\n%sFilters%s\n", ANSI_BOLD, ANSI_RESET);
    mk_list_foreach(head, &config->filter_plugins) {
        filter = mk_list_entry(head, struct flb_filter_plugin, _head);
        print_opt(filter->name, filter->description);
    }

    printf("\n%sOutputs%s\n", ANSI_BOLD, ANSI_RESET);
    mk_list_foreach(head, &config->out_plugins) {
        out = mk_list_entry(head, struct flb_output_plugin, _head);
        if (strcmp(out->name, "lib") == 0 || (out->flags & FLB_OUTPUT_PRIVATE)) {
            /* useless..., just skip it. */
            continue;
        }
        print_opt(out->name, out->description);
    }

    printf("\n%sInternal%s\n", ANSI_BOLD, ANSI_RESET);
    printf(" Event Loop  = %s\n", mk_event_backend());
    printf(" Build Flags =%s\n", FLB_INFO_FLAGS);
    exit(rc);
}

/*
 * If the description is larger than the allowed 80 chars including left
 * padding, split the content in multiple lines and align it properly.
 */
static void help_plugin_description(int left_padding, flb_sds_t str)
{
    int len;
    int max;
    int line = 0;
    char *c;
    char *p;
    char *end;
    char fmt[32];

    if (!str) {
        printf("no description available\n");
        return;
    }

    max = 90 - left_padding;
    len = strlen(str);

    if (len <= max) {
        printf("%s\n", str);
        return;
    }

    p = str;
    len = flb_sds_len(str);
    end = str + len;

    while (p < end) {
        if ((p + max) > end) {
            c = end;
        }
        else {
            c = p + max;
            while (*c != ' ' && c > p) {
                c--;
            }
        }

        if (c == p) {
            len = end - p;
        }
        else {
            len = c - p;
        }

        snprintf(fmt, sizeof(fmt) - 1, "%%*s%%.%is\n", len);
        if (line == 0) {
            printf(fmt, 0, "", p);
        }
        else {
            printf(fmt, left_padding, " ", p);
        }
        line++;
        p += len + 1;
    }
}

static msgpack_object *help_get_obj(msgpack_object map, char *key)
{
    flb_sds_t k;
    msgpack_object *o;
    struct flb_ra_value *rval = NULL;
    struct flb_record_accessor *ra = NULL;

    k = flb_sds_create(key);
    ra = flb_ra_create(k, FLB_FALSE);
    flb_sds_destroy(k);
    if (!ra) {
        return NULL;
    }

    rval = flb_ra_get_value_object(ra, map);
    if (!rval) {
        flb_ra_destroy(ra);
        return NULL;
    }

    o = &rval->o;
    flb_ra_key_value_destroy(rval);
    flb_ra_destroy(ra);

    return o;
}

static flb_sds_t help_get_value(msgpack_object map, char *key)
{
    flb_sds_t val;
    msgpack_object *o;

    o = help_get_obj(map, key);
    val = flb_sds_create_len(o->via.str.ptr, o->via.str.size);
    return val;
}

static void help_print_property(int max, msgpack_object k, msgpack_object v)
{
    int i;
    int len = 0;
    char buf[32];
    char fmt[32];
    char fmt_prf[32];
    char def[32];
    msgpack_object map;
    flb_sds_t tmp;
    flb_sds_t name;
    flb_sds_t type;
    flb_sds_t desc;
    flb_sds_t defv;

    /* Convert property type to uppercase and print it */
    for (i = 0; i < k.via.str.size; i++) {
        buf[i] = toupper(k.via.str.ptr[i]);
    }
    buf[k.via.str.size] = '\0';
    printf(ANSI_BOLD "\n%s\n" ANSI_RESET, buf);

    snprintf(fmt, sizeof(fmt) - 1, "%%-%is", max);
    snprintf(fmt_prf, sizeof(fmt_prf) - 1, "%%-%is", max);
    snprintf(def, sizeof(def) - 1, "%%*s> default: %%s, type: ");

    for (i = 0; i < v.via.array.size; i++) {
        map = v.via.array.ptr[i];

        name = help_get_value(map, "$name");
        type = help_get_value(map, "$type");
        desc = help_get_value(map, "$description");
        defv = help_get_value(map, "$default");

        if (strcmp(type, "prefix") == 0) {
            len = flb_sds_len(name);
            tmp = flb_sds_create_size(len + 2);
            flb_sds_printf(&tmp, "%sN", name);
            printf(fmt_prf, tmp);
            flb_sds_destroy(tmp);
        }
        else {
            printf(fmt, name);
        }

        help_plugin_description(max, desc);

        if (defv) {
            printf(def, max, " ", defv);
        }
        else {
            printf("%*s> type: ", max, " ");
        }
        printf("%s", type);
        printf("\n\n");
    }
}

static void help_format_json(void *help_buf, size_t help_size)
{
    flb_sds_t json;

    json = flb_msgpack_raw_to_json_sds(help_buf, help_size);
    printf("%s\n", json);
    flb_sds_destroy(json);
}

static void help_format_text(void *help_buf, size_t help_size)
{
    int i;
    int x;
    int max = 0;
    int len = 0;
    int ret;
    size_t off = 0;
    flb_sds_t name;
    flb_sds_t type;
    flb_sds_t desc;
    msgpack_unpacked result;
    msgpack_object map;
    msgpack_object p;
    msgpack_object k;
    msgpack_object v;

    msgpack_unpacked_init(&result);
    ret = msgpack_unpack_next(&result, help_buf, help_size, &off);
    if (ret != MSGPACK_UNPACK_SUCCESS) {
        return;
    }
    map = result.data;

    type = help_get_value(map, "$type");
    name = help_get_value(map, "$name");
    desc = help_get_value(map, "$description");

    printf("%sHELP%s\n%s %s plugin\n", ANSI_BOLD, ANSI_RESET,
           name, type);
    flb_sds_destroy(type);
    flb_sds_destroy(name);

    if (desc) {
        printf(ANSI_BOLD "\nDESCRIPTION\n" ANSI_RESET "%s\n", desc);
        flb_sds_destroy(desc);
    }

    /* Properties */
    p = map.via.map.ptr[3].val;

    /* Calculate padding */
    for (i = 0; i < p.via.map.size; i++) {
        v = p.via.map.ptr[i].val;
        for (x = 0; x < v.via.map.size; x++) {
            msgpack_object ptr = v.via.array.ptr[x];
            name = help_get_value(ptr, "$name");
            len = flb_sds_len(name);
            flb_sds_destroy(name);
            if (len > max) {
                max = len;
            }
        }
    }
    max += 2;

    /* Iterate each section of properties */
    for (i = 0; i < p.via.map.size; i++) {
        k = p.via.map.ptr[i].key;
        v = p.via.map.ptr[i].val;
        help_print_property(max, k, v);
    }
}

static void flb_help_plugin(int rc, int format,
                            struct flb_config *config, int type,
                            struct flb_input_instance *in,
                            struct flb_filter_instance *filter,
                            struct flb_output_instance *out)


{
    struct flb_config_map *m = NULL;
    struct flb_config_map *opt = NULL;
    void *help_buf;
    size_t help_size;

    flb_banner();

    if (type == PLUGIN_INPUT) {
        opt = in->p->config_map;
        flb_help_input(in, &help_buf, &help_size);
    }
    else if (type == PLUGIN_FILTER) {
        opt = filter->p->config_map;
        flb_help_filter(filter, &help_buf, &help_size);
    }
    else if (type == PLUGIN_OUTPUT) {
        opt = m = out->p->config_map;
        flb_help_output(out, &help_buf, &help_size);
    }

    if (!opt) {
        exit(rc);
    }

    if (format == FLB_HELP_TEXT) {
        help_format_text(help_buf, help_size);
    }
    else if (format == FLB_HELP_JSON) {
        help_format_json(help_buf, help_size);
    }
    flb_free(help_buf);

    exit(rc);
}

#define flb_print_signal(X) case X:                       \
    write (STDERR_FILENO, #X ")\n", sizeof(#X ")\n")-1); \
    break;

static void flb_signal_handler_break_loop(int signal)
{
    exit_signal = signal;
}

static void flb_signal_exit(int signal)
{
    int len;
    char ts[32];
    char s[] = "[engine] caught signal (";
    time_t now;
    struct tm *cur;

    now = time(NULL);
    cur = localtime(&now);
    len = snprintf(ts, sizeof(ts) - 1, "[%i/%02i/%02i %02i:%02i:%02i] ",
                   cur->tm_year + 1900,
                   cur->tm_mon + 1,
                   cur->tm_mday,
                   cur->tm_hour,
                   cur->tm_min,
                   cur->tm_sec);

    /* write signal number */
    write(STDERR_FILENO, ts, len);
    write(STDERR_FILENO, s, sizeof(s) - 1);
    switch (signal) {
        flb_print_signal(SIGINT);
#ifndef FLB_SYSTEM_WINDOWS
        flb_print_signal(SIGQUIT);
        flb_print_signal(SIGHUP);
        flb_print_signal(SIGCONT);
#endif
        flb_print_signal(SIGTERM);
        flb_print_signal(SIGSEGV);
    };

    /* Signal handlers */
    /* SIGSEGV is not handled here to preserve stacktrace */
    switch (signal) {
    case SIGINT:
    case SIGTERM:
#ifndef FLB_SYSTEM_WINDOWS
    case SIGQUIT:
    case SIGHUP:
#endif
        flb_stop(ctx);
        flb_destroy(ctx);
        _exit(EXIT_SUCCESS);
    default:
        break;
    }
}

static void flb_signal_handler(int signal)
{
    int len;
    char ts[32];
    char s[] = "[engine] caught signal (";
    time_t now;
    struct tm *cur;

    now = time(NULL);
    cur = localtime(&now);
    len = snprintf(ts, sizeof(ts) - 1, "[%i/%02i/%02i %02i:%02i:%02i] ",
                   cur->tm_year + 1900,
                   cur->tm_mon + 1,
                   cur->tm_mday,
                   cur->tm_hour,
                   cur->tm_min,
                   cur->tm_sec);

    /* write signal number */
    write(STDERR_FILENO, ts, len);
    write(STDERR_FILENO, s, sizeof(s) - 1);
    switch (signal) {
        flb_print_signal(SIGINT);
#ifndef FLB_SYSTEM_WINDOWS
        flb_print_signal(SIGQUIT);
        flb_print_signal(SIGHUP);
        flb_print_signal(SIGCONT);
#endif
        flb_print_signal(SIGTERM);
        flb_print_signal(SIGSEGV);
        flb_print_signal(SIGFPE);
    };

    switch(signal) {
    case SIGSEGV:
    case SIGFPE:
#ifdef FLB_HAVE_LIBBACKTRACE
        /* To preserve stacktrace */
        flb_stacktrace_print(&flb_st);
#endif
        abort();
#ifndef FLB_SYSTEM_WINDOWS
    case SIGCONT:
        flb_dump(ctx->config);
#endif
    }
}

static void flb_signal_init()
{
    signal(SIGINT,  &flb_signal_handler_break_loop);
#ifndef FLB_SYSTEM_WINDOWS
    signal(SIGQUIT, &flb_signal_handler_break_loop);
    signal(SIGHUP,  &flb_signal_handler_break_loop);
    signal(SIGCONT, &flb_signal_handler);
#endif
    signal(SIGTERM, &flb_signal_handler_break_loop);
    signal(SIGSEGV, &flb_signal_handler);
    signal(SIGFPE,  &flb_signal_handler);
}

static int custom_set_property(struct flb_custom_instance *in, char *kv)
{
    int ret;
    int len;
    int sep;
    char *key;
    char *value;

    len = strlen(kv);
    sep = mk_string_char_search(kv, '=', len);
    if (sep == -1) {
        return -1;
    }

    key = mk_string_copy_substr(kv, 0, sep);
    value = kv + sep + 1;

    if (!key) {
        return -1;
    }

    ret = flb_custom_set_property(in, key, value);
    if (ret == -1) {
        fprintf(stderr, "[error] setting up '%s' plugin property '%s'\n",
                in->p->name, key);
    }

    mk_mem_free(key);
    return ret;
}

static int input_set_property(struct flb_input_instance *in, char *kv)
{
    int ret;
    int len;
    int sep;
    char *key;
    char *value;

    len = strlen(kv);
    sep = mk_string_char_search(kv, '=', len);
    if (sep == -1) {
        return -1;
    }

    key = mk_string_copy_substr(kv, 0, sep);
    value = kv + sep + 1;

    if (!key) {
        return -1;
    }

    ret = flb_input_set_property(in, key, value);
    if (ret == -1) {
        fprintf(stderr, "[error] setting up '%s' plugin property '%s'\n",
                in->p->name, key);
    }

    mk_mem_free(key);
    return ret;
}

static int output_set_property(struct flb_output_instance *out, char *kv)
{
    int ret;
    int len;
    int sep;
    char *key;
    char *value;
    len = strlen(kv);
    sep = mk_string_char_search(kv, '=', len);
    if (sep == -1) {
        return -1;
    }

    key = mk_string_copy_substr(kv, 0, sep);
    value = kv + sep + 1;

    if (!key) {
        return -1;
    }

    ret = flb_output_set_property(out, key, value);
    mk_mem_free(key);
    return ret;
}

static int filter_set_property(struct flb_filter_instance *filter, char *kv)
{
    int ret;
    int len;
    int sep;
    char *key;
    char *value;

    len = strlen(kv);
    sep = mk_string_char_search(kv, '=', len);
    if (sep == -1) {
        return -1;
    }

    key = mk_string_copy_substr(kv, 0, sep);
    value = kv + sep + 1;

    if (!key) {
        return -1;
    }

    ret = flb_filter_set_property(filter, key, value);
    mk_mem_free(key);
    return ret;
}

static void flb_service_conf_err(struct mk_rconf_section *section, char *key)
{
    fprintf(stderr, "Invalid configuration value at %s.%s\n",
            section->name, key);
}

static int flb_service_conf_path_set(struct flb_config *config, char *file)
{
    char *end;
    char *path;

    path = realpath(file, NULL);
    if (!path) {
        return -1;
    }

    /* lookup path ending and truncate */
    end = strrchr(path, FLB_DIRCHAR);
    if (!end) {
        free(path);
        return -1;
    }

    end++;
    *end = '\0';
    config->conf_path = flb_strdup(path);
    free(path);

    return 0;
}

static int flb_service_conf(struct flb_config *config, char *file)
{
    int ret = -1;
    char *tmp;
    char *name;
    struct mk_list *head;
    struct mk_list *h_prop;
    struct mk_rconf *fconf = NULL;
    struct mk_rconf_entry *entry;
    struct mk_rconf_section *section;
    struct flb_custom_instance *custom;
    struct flb_input_instance *in;
    struct flb_output_instance *out;
    struct flb_filter_instance *filter;

#ifdef FLB_HAVE_STATIC_CONF
    fconf = flb_config_static_open(file);
#else
    fconf = mk_rconf_open(file);
#endif

    if (!fconf) {
        return -1;
    }

    /* Process all meta commands */
    mk_list_foreach(head, &fconf->metas) {
        entry = mk_list_entry(head, struct mk_rconf_entry, _head);
        flb_meta_run(config, entry->key, entry->val);
    }

    /* Set configuration root path */
    flb_service_conf_path_set(config, file);

    /* Validate sections */
    mk_list_foreach(head, &fconf->sections) {
        section = mk_list_entry(head, struct mk_rconf_section, _head);

        if (strcasecmp(section->name, "SERVICE") == 0 ||
            strcasecmp(section->name, "CUSTOM") == 0 ||
            strcasecmp(section->name, "INPUT") == 0 ||
            strcasecmp(section->name, "FILTER") == 0 ||
            strcasecmp(section->name, "OUTPUT") == 0) {

            /* continue on valid sections */
            continue;
        }

        /* Extra sanity checks */
        if (strcasecmp(section->name, "PARSER") == 0 ||
            strcasecmp(section->name, "MULTILINE_PARSER") == 0) {
            fprintf(stderr,
                    "Sections [MULTILINE_PARSER] and [PARSER] are not valid in "
                    "the main configuration file. It belongs to \n"
                    "the 'parsers_file' configuration files.\n");
        }
        else {
            fprintf(stderr,
                    "Error: unexpected section [%s] in the main "
                    "configuration file.\n", section->name);
        }
        exit(EXIT_FAILURE);
    }

    /* Read main [SERVICE] section */
    section = mk_rconf_section_get(fconf, "SERVICE");
    if (section) {
        /* Iterate properties */
        mk_list_foreach(h_prop, &section->entries) {
            entry = mk_list_entry(h_prop, struct mk_rconf_entry, _head);
            /* Set the property */
            flb_config_set_property(config, entry->key, entry->val);
        }
    }

    /* Read all [CUSTOM] sections */
    mk_list_foreach(head, &fconf->sections) {
        section = mk_list_entry(head, struct mk_rconf_section, _head);
        if (strcasecmp(section->name, "CUSTOM") != 0) {
            continue;
        }

        /* Get the input plugin name */
        name = s_get_key(section, "name", MK_RCONF_STR);
        if (!name) {
            flb_service_conf_err(section, "name");
            goto flb_service_conf_end;
        }

        flb_debug("[service] loading custom plugin: %s", name);

        /* Create an instace of the plugin */
        tmp = flb_env_var_translate(config->env, name);
        custom = flb_custom_new(config, tmp, NULL);
        mk_mem_free(name);
        if (!custom) {
            fprintf(stderr, "Custom plugin '%s' cannot be loaded\n", tmp);
            flb_sds_destroy(tmp);
            goto flb_service_conf_end;
        }
        flb_sds_destroy(tmp);

        /* Iterate other properties */
        mk_list_foreach(h_prop, &section->entries) {
            entry = mk_list_entry(h_prop, struct mk_rconf_entry, _head);
            if (strcasecmp(entry->key, "name") == 0) {
                continue;
            }

            /* Set the property */
            ret = flb_custom_set_property(custom, entry->key, entry->val);
            if (ret == -1) {
                fprintf(stderr, "Error setting up %s plugin property '%s'\n",
                        custom->name, entry->key);
                goto flb_service_conf_end;
            }
        }
    }

    /* Read all [INPUT] sections */
    mk_list_foreach(head, &fconf->sections) {
        section = mk_list_entry(head, struct mk_rconf_section, _head);
        if (strcasecmp(section->name, "INPUT") != 0) {
            continue;
        }

        /* Get the input plugin name */
        name = s_get_key(section, "Name", MK_RCONF_STR);
        if (!name) {
            flb_service_conf_err(section, "Name");
            goto flb_service_conf_end;
        }

        flb_debug("[service] loading input: %s", name);

        /* Create an instace of the plugin */
        tmp = flb_env_var_translate(config->env, name);
        in = flb_input_new(config, tmp, NULL, FLB_TRUE);
        mk_mem_free(name);
        if (!in) {
            fprintf(stderr, "Input plugin '%s' cannot be loaded\n", tmp);
            flb_sds_destroy(tmp);
            goto flb_service_conf_end;
        }
        flb_sds_destroy(tmp);

        /* Iterate other properties */
        mk_list_foreach(h_prop, &section->entries) {
            entry = mk_list_entry(h_prop, struct mk_rconf_entry, _head);
            if (strcasecmp(entry->key, "Name") == 0) {
                continue;
            }

            /* Set the property */
            ret = flb_input_set_property(in, entry->key, entry->val);
            if (ret == -1) {
                fprintf(stderr, "Error setting up %s plugin property '%s'\n",
                        in->name, entry->key);
                goto flb_service_conf_end;
            }
        }
    }

    /* Read all [OUTPUT] sections */
    mk_list_foreach(head, &fconf->sections) {
        section = mk_list_entry(head, struct mk_rconf_section, _head);
        if (strcasecmp(section->name, "OUTPUT") != 0) {
            continue;
        }

        /* Get the output plugin name */
        name = s_get_key(section, "Name", MK_RCONF_STR);
        if (!name) {
            flb_service_conf_err(section, "Name");
            goto flb_service_conf_end;
        }

        /* Create an instace of the plugin */
        tmp = flb_env_var_translate(config->env, name);
        out = flb_output_new(config, tmp, NULL, FLB_TRUE);
        mk_mem_free(name);
        if (!out) {
            fprintf(stderr, "Output plugin '%s' cannot be loaded\n", tmp);
            flb_sds_destroy(tmp);
            goto flb_service_conf_end;
        }
        flb_sds_destroy(tmp);

        /* Iterate other properties */
        mk_list_foreach(h_prop, &section->entries) {
            entry = mk_list_entry(h_prop, struct mk_rconf_entry, _head);
            if (strcasecmp(entry->key, "Name") == 0) {
                continue;
            }

            /* Set the property */
            flb_output_set_property(out, entry->key, entry->val);
        }
    }

    /* Read all [FILTER] sections */
    mk_list_foreach(head, &fconf->sections) {
        section = mk_list_entry(head, struct mk_rconf_section, _head);
        if (strcasecmp(section->name, "FILTER") != 0) {
            continue;
        }
        /* Get the filter plugin name */
        name = s_get_key(section, "Name", MK_RCONF_STR);
        if (!name) {
            flb_service_conf_err(section, "Name");
            goto flb_service_conf_end;
        }
        /* Create an instace of the plugin */
        tmp = flb_env_var_translate(config->env, name);
        filter = flb_filter_new(config, tmp, NULL);
        flb_sds_destroy(tmp);
        mk_mem_free(name);
        if (!filter) {
            flb_service_conf_err(section, "Name");
            goto flb_service_conf_end;
        }

        /* Iterate other properties */
        mk_list_foreach(h_prop, &section->entries) {
            entry = mk_list_entry(h_prop, struct mk_rconf_entry, _head);
            if (strcasecmp(entry->key, "Name") == 0) {
                continue;
            }

            /* Set the property */
            flb_filter_set_property(filter, entry->key, entry->val);
        }
    }

    ret = 0;

flb_service_conf_end:
    if (fconf != NULL) {
        mk_rconf_free(fconf);
    }
    return ret;
}

int flb_main(int argc, char **argv)
{
    int opt;
    int ret;

    /* handle plugin properties:  -1 = none, 0 = input, 1 = output */
    int last_plugin = -1;

    /* local variables to handle config options */
    char *cfg_file = NULL;
    struct flb_custom_instance *custom = NULL;
    struct flb_input_instance *in = NULL;
    struct flb_output_instance *out = NULL;
    struct flb_filter_instance *filter = NULL;

#ifdef FLB_HAVE_LIBBACKTRACE
    flb_stacktrace_init(argv[0], &flb_st);
#endif

    /* Setup long-options */
    static const struct option long_opts[] = {
        { "storage_path",    required_argument, NULL, 'b' },
        { "config",          required_argument, NULL, 'c' },
#ifdef FLB_HAVE_FORK
        { "daemon",          no_argument      , NULL, 'd' },
#endif
        { "dry-run",         no_argument      , NULL, 'D' },
        { "flush",           required_argument, NULL, 'f' },
        { "http",            no_argument      , NULL, 'H' },
        { "log_file",        required_argument, NULL, 'l' },
        { "port",            required_argument, NULL, 'P' },
        { "custom",          required_argument, NULL, 'C' },
        { "input",           required_argument, NULL, 'i' },
        { "match",           required_argument, NULL, 'm' },
        { "output",          required_argument, NULL, 'o' },
        { "filter",          required_argument, NULL, 'F' },
#ifdef FLB_HAVE_PARSER
        { "parser",          required_argument, NULL, 'R' },
#endif
        { "prop",            required_argument, NULL, 'p' },
        { "plugin",          required_argument, NULL, 'e' },
        { "tag",             required_argument, NULL, 't' },
#ifdef FLB_HAVE_STREAM_PROCESSOR
        { "sp-task",         required_argument, NULL, 'T' },
#endif
        { "version",         no_argument      , NULL, 'V' },
        { "verbose",         no_argument      , NULL, 'v' },
        { "workdir",         required_argument, NULL, 'w' },
        { "quiet",           no_argument      , NULL, 'q' },
        { "help",            no_argument      , NULL, 'h' },
        { "help-json",       no_argument      , NULL, 'J' },
        { "coro_stack_size", required_argument, NULL, 's' },
        { "sosreport",       no_argument      , NULL, 'S' },
#ifdef FLB_HAVE_HTTP_SERVER
        { "http_server",     no_argument      , NULL, 'H' },
        { "http_listen",     required_argument, NULL, 'L' },
        { "http_port",       required_argument, NULL, 'P' },
#endif
        { NULL, 0, NULL, 0 }
    };

    /* Signal handler */
    flb_signal_init();

    /* Initialize Monkey Core library */
    mk_core_init();

    /* Create Fluent Bit context */
    ctx = flb_create();
    if (!ctx) {
        exit(EXIT_FAILURE);
    }
    config = ctx->config;

#ifndef FLB_HAVE_STATIC_CONF

    /* Parse the command line options */
    while ((opt = getopt_long(argc, argv,
                              "b:c:dDf:C:i:m:o:R:F:p:e:"
                              "t:T:l:vw:qVhJL:HP:s:S",
                              long_opts, NULL)) != -1) {

        switch (opt) {
        case 'b':
            config->storage_path = flb_strdup(optarg);
            break;
        case 'c':
            cfg_file = flb_strdup(optarg);
            break;
#ifdef FLB_HAVE_FORK
        case 'd':
            config->daemon = FLB_TRUE;
            break;
#endif
        case 'D':
            config->dry_run = FLB_TRUE;
            break;
        case 'e':
            ret = flb_plugin_load_router(optarg, config);
            if (ret == -1) {
                exit(EXIT_FAILURE);
            }
            break;
        case 'f':
            config->flush = atof(optarg);
            break;
        case 'C':
            custom = flb_custom_new(config, optarg, NULL);
            if (!custom) {
                flb_utils_error(FLB_ERR_CUSTOM_INVALID);
            }
            last_plugin = PLUGIN_CUSTOM;
            break;
        case 'i':
            in = flb_input_new(config, optarg, NULL, FLB_TRUE);
            if (!in) {
                flb_utils_error(FLB_ERR_INPUT_INVALID);
            }
            last_plugin = PLUGIN_INPUT;
            break;
        case 'm':
            if (last_plugin == PLUGIN_FILTER) {
                flb_filter_set_property(filter, "match", optarg);
            }
            else if (last_plugin == PLUGIN_OUTPUT) {
                flb_output_set_property(out, "match", optarg);
            }
            break;
        case 'o':
            out = flb_output_new(config, optarg, NULL, FLB_TRUE);
            if (!out) {
                flb_utils_error(FLB_ERR_OUTPUT_INVALID);
            }
            last_plugin = PLUGIN_OUTPUT;
            break;
#ifdef FLB_HAVE_PARSER
        case 'R':
            ret = flb_parser_conf_file(optarg, config);
            if (ret != 0) {
                exit(EXIT_FAILURE);
            }
            break;
#endif
        case 'F':
            filter = flb_filter_new(config, optarg, NULL);
            if (!filter) {
                flb_utils_error(FLB_ERR_FILTER_INVALID);
            }
            last_plugin = PLUGIN_FILTER;
            break;
        case 'l':
            config->log_file = flb_strdup(optarg);
            break;
        case 'p':
            if (last_plugin == PLUGIN_INPUT) {
                ret = input_set_property(in, optarg);
                if (ret != 0) {
                    exit(EXIT_FAILURE);
                }
            }
            else if (last_plugin == PLUGIN_OUTPUT) {
                output_set_property(out, optarg);
            }
            else if (last_plugin == PLUGIN_FILTER) {
                filter_set_property(filter, optarg);
            }
            else if (last_plugin == PLUGIN_CUSTOM) {
                custom_set_property(custom, optarg);
            }
            break;
        case 't':
            if (in) {
                flb_input_set_property(in, "tag", optarg);
            }
            break;
#ifdef FLB_HAVE_STREAM_PROCESSOR
        case 'T':
            flb_slist_add(&config->stream_processor_tasks, optarg);
            break;
#endif
        case 'h':
            if (last_plugin == -1) {
                flb_help(EXIT_SUCCESS, config);
            }
            else {
                flb_help_plugin(EXIT_SUCCESS, FLB_HELP_TEXT,
                                config,
                                last_plugin, in, filter, out);
            }
            break;
        case 'J':
            if (last_plugin == -1) {
                flb_help(EXIT_SUCCESS, config);
            }
            else {
                flb_help_plugin(EXIT_SUCCESS, FLB_HELP_JSON, config,
                                last_plugin, in, filter, out);
            }
            break;
#ifdef FLB_HAVE_HTTP_SERVER
        case 'H':
            config->http_server = FLB_TRUE;
            break;
        case 'L':
            if (config->http_listen) {
                flb_free(config->http_listen);
            }
            config->http_listen = flb_strdup(optarg);
            break;
        case 'P':
            if (config->http_port) {
                flb_free(config->http_port);
            }
            config->http_port = flb_strdup(optarg);
            break;
#endif
        case 'V':
            flb_version();
            exit(EXIT_SUCCESS);
        case 'v':
            config->verbose++;
            break;
        case 'w':
            config->workdir =  flb_strdup(optarg);
            break;
        case 'q':
            config->verbose = FLB_LOG_OFF;
            break;
        case 's':
            config->coro_stack_size = (unsigned int) atoi(optarg);
            break;
        case 'S':
            config->support_mode = FLB_TRUE;
            break;
        default:
            flb_help(EXIT_FAILURE, config);
        }
    }
#endif /* !FLB_HAVE_STATIC_CONF */

    set_log_level_from_env(config);

    if (config->verbose != FLB_LOG_OFF) {
        flb_banner();
    }

    /* Program name */
    flb_config_set_program_name(config, argv[0]);

    /* Set the current directory */
    if (config->workdir) {
        ret = chdir(config->workdir);
        if (ret == -1) {
            flb_errno();
            return -1;
        }
    }

    /* Validate config file */
#ifndef FLB_HAVE_STATIC_CONF
    if (cfg_file) {
        if (access(cfg_file, R_OK) != 0) {
            flb_utils_error(FLB_ERR_CFG_FILE);
        }

        /* Load the service configuration file */
        ret = flb_service_conf(config, cfg_file);
        if (ret != 0) {
            flb_utils_error(FLB_ERR_CFG_FILE_STOP);
        }
        flb_free(cfg_file);
    }
#else
    ret = flb_service_conf(config, "fluent-bit.conf");
    if (ret != 0) {
        flb_utils_error(FLB_ERR_CFG_FILE_STOP);
    }
#endif

    /* Check co-routine stack size */
    if (config->coro_stack_size < getpagesize()) {
        flb_utils_error(FLB_ERR_CORO_STACK_SIZE);
    }

    /* Validate flush time (seconds) */
    if (config->flush <= (double) 0.0) {
        flb_utils_error(FLB_ERR_CFG_FLUSH);
    }

    /* debug or trace */
    if (config->verbose >= FLB_LOG_DEBUG) {
        flb_utils_print_setup(config);
    }

#ifdef FLB_HAVE_FORK
    /* Run in background/daemon mode */
    if (config->daemon == FLB_TRUE) {
        flb_utils_set_daemon(config);
    }
#endif

#ifdef FLB_SYSTEM_WINDOWS
    win32_started();
#endif

    if (config->dry_run == FLB_TRUE) {
        fprintf(stderr, "configuration test is successful\n");
        exit(EXIT_SUCCESS);
    }

    ret = flb_start(ctx);
    if (ret != 0) {
        flb_destroy(ctx);
        return ret;
    }

    while (ctx->status == FLB_LIB_OK && exit_signal == 0) {
        sleep(1);
    }
    if (exit_signal) {
        flb_signal_exit(exit_signal);
    }
    ret = config->exit_status_code;
    flb_destroy(ctx);


    return ret;
}

int main(int argc, char **argv)
{
#ifdef FLB_SYSTEM_WINDOWS
    return win32_main(argc, argv);
#else
    return flb_main(argc, argv);
#endif
}
