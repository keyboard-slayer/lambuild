#include <assert.h>
#include <errno.h>
#include <dirent.h>
#include <libgen.h>
#include <libguile.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/stat.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

struct source_object_pair 
{
    char *source;
    char *object;
};

struct compile_args 
{
    char *cc;
    char **cflags;
    struct source_object_pair *file_out;
    size_t pair_count;
    SCM *obj_files;
};

static char **scm_to_string_list(SCM list)
{
    assert(!SCM_UNBNDP(list));

    size_t length = scm_to_size_t(scm_length(list));

    char **ret = malloc(sizeof(char *) * (length + 1));
    assert(ret != NULL);

    for (size_t i = 0; i < length; i++)
    {
        SCM item = scm_list_ref(list, scm_from_size_t(i));
        ret[i] = scm_to_stringn(item, NULL, "ascii", SCM_FAILED_CONVERSION_ERROR);
    }

    ret[length] = NULL;

    return ret;
}

static void scm_str_list_free(char **lst)
{
    for (size_t i = 0; lst[i] != NULL; i++)
    {
        free(lst[i]);
    }

    free(lst);
}

static size_t array_length(char **array, int to_add)
{
    if (array == NULL)
    {
        return 0;
    }

    size_t length = 0;
    for (size_t i = 0; array[i] != NULL; i++)
    {
        length += strlen(array[i]) + to_add;
    }

    return length;
}

static bool need_to_compile(struct source_object_pair info)
{
    struct stat file_attr;
    struct stat out_attr;

    if (access(info.object, F_OK) != 0)
    {
        return true;
    }

    stat(info.source, &file_attr);
    stat(info.object, &out_attr);

    return file_attr.st_mtime > out_attr.st_mtime;
}

static void mkparent(char const *s)
{
    char *path = strdup(s);
    char *parent_path = dirname(path);
    char *buffer = calloc(strlen(parent_path) + 10, 1);

    strcat(buffer, "mkdir -p ");
    strcat(buffer, parent_path);

    system(buffer);

    free(buffer);
    free(path);
}

static void *compile_c_file(void *data)
{
    struct compile_args *arg = data;

    for (size_t i = 0; i < arg->pair_count; i++)
    {
        mkparent(arg->file_out[i].object);

        while (pthread_mutex_lock(&lock));

        SCM new_obj = scm_list_1(
            scm_from_stringn(arg->file_out[i].object, strlen(arg->file_out[i].object), "ascii", SCM_FAILED_CONVERSION_ERROR));

        SCM argu = scm_list_2(*arg->obj_files, new_obj);
        *arg->obj_files = scm_append(argu);

        pthread_mutex_unlock(&lock);

        if (!need_to_compile(arg->file_out[i]))
        {
            return NULL;
        }

        size_t length = strlen(arg->cc) + strlen(arg->file_out[i].source) + strlen(arg->file_out[i].object) + array_length(arg->cflags, 1) + 30;

        char *buf = calloc(length, 1);
        memcpy(buf, arg->cc, strlen(arg->cc));
        strcat(buf, " ");

        for (size_t i = 0; arg->cflags != NULL && arg->cflags[i] != NULL; i++)
        {
            strcat(buf, arg->cflags[i]);
            strcat(buf, " ");
        }

        strcat(buf, arg->file_out[i].source);
        strcat(buf, " ");
        strcat(buf, "-c");
        strcat(buf, " ");
        strcat(buf, "-o");
        strcat(buf, " ");
        strcat(buf, arg->file_out[i].object);
        strcat(buf, " ");
        strcat(buf, ">/dev/null 2>.err");


        if (system(buf) != 0)
        {
            system("less .err");
            exit(1);
        }

        system("rm .err");
        free(buf);
    }

    return NULL;
}

static SCM compile_files(SCM args)
{
    SCM cc_keyword = scm_from_utf8_keyword("cc");
    SCM cflags_keyword = scm_from_utf8_keyword("cflags");
    SCM src_keyword = scm_from_utf8_keyword("src");
    SCM out_keyword = scm_from_utf8_keyword("bindir");
    SCM threading_keyword = scm_from_utf8_keyword("jobs");

    SCM cc_scm = SCM_UNDEFINED;
    SCM cflags_scm = SCM_UNDEFINED;
    SCM src_scm = SCM_UNDEFINED;
    SCM output_dir_scm = SCM_UNDEFINED;
    SCM threading_scm = SCM_UNDEFINED;

    scm_c_bind_keyword_arguments("compile-files", args, 0,  cc_keyword, &cc_scm, cflags_keyword, &cflags_scm, 
                            src_keyword, &src_scm, out_keyword, &output_dir_scm, threading_keyword, &threading_scm);

    char *output_dir;
    char *cc;

    char **src = scm_to_string_list(src_scm);
    char **cflags = NULL;

    if (SCM_UNBNDP(cc_scm))
    {
        cc = strdup("gcc");
    }
    else  
    {
        cc = scm_to_stringn(cc_scm, NULL, "ascii", SCM_FAILED_CONVERSION_ERROR);
    }

    if (SCM_UNBNDP(output_dir_scm))
    {
        output_dir = strdup("build");
    }
    else  
    {
        output_dir = scm_to_stringn(output_dir_scm, NULL, "ascii", SCM_FAILED_CONVERSION_ERROR);
    }

    if (SCM_UNBNDP(cflags_scm))
    {
        cflags = NULL;
    }
    else  
    {
        cflags = scm_to_string_list(cflags_scm);
    }

    SCM obj_files = SCM_LIST0;

    if (SCM_UNBNDP(threading_scm))
    {
        for (size_t i = 0; i < scm_to_size_t(scm_length(src_scm)); i++)
        {
            char *filename = calloc(strlen(src[i]) + strlen(output_dir) + 4 , 1);
            sprintf(filename, "%s/%s.o", output_dir, src[i]);

            struct source_object_pair out = {src[i], filename};
            struct source_object_pair *pair = malloc(sizeof(struct source_object_pair));
            memcpy(pair, &out, sizeof(struct source_object_pair));

            struct compile_args args = {cc, cflags, pair, 1, &obj_files};
            compile_c_file(&args);

            free(pair);
            free(filename);
        }
    }
    else  
    {
        size_t jobs = scm_to_size_t(threading_scm);
        size_t file_per_job = ceil((double) scm_to_size_t(scm_length(src_scm)) / jobs);

        pthread_t *threads = malloc(sizeof(pthread_t) * jobs);
        struct compile_args *args_per_job = malloc(sizeof(struct compile_args) * jobs);

        for (size_t i = 0; i < jobs; i++)
        {
            args_per_job[i].file_out = calloc(file_per_job, sizeof(struct source_object_pair));
            args_per_job[i].pair_count = 0;
        }

        for (size_t i = 0; src[i] != NULL; i++)
        {
            char *filename = calloc(strlen(src[i]) + strlen(output_dir) + 4 , 1);
            sprintf(filename, "%s/%s.o", output_dir, src[i]);

            struct compile_args *job = &args_per_job[i % jobs];
            job->file_out[job->pair_count++] = (struct source_object_pair) {src[i], filename};
        }

        for (size_t i = 0; i < jobs; i++)
        {
            pthread_create(&threads[i], NULL, &compile_c_file, &args_per_job[i]);
        }

        for (size_t i = 0; i < jobs; i++) 
        {
            for (size_t j = 0; j < args_per_job[i].pair_count; j++)
            {
                if (args_per_job[i].file_out[j].object != NULL)
                {
                    free(args_per_job[i].file_out[j].object);
                }
            }
        }

        free(args_per_job);
        free(threads);
    }

    scm_str_list_free(src);

    if (cflags != NULL)
    {
        scm_str_list_free(cflags);
    }

    free(cc);
    free(output_dir);

    return obj_files;
}

static SCM link_executable(SCM args)
{
    SCM ld_keyword = scm_from_utf8_keyword("ld");
    SCM ldflags_keyword = scm_from_utf8_keyword("ldflags");
    SCM obj_keyword = scm_from_utf8_keyword("objs");
    SCM target_keyword = scm_from_utf8_keyword("target");

    SCM ld_scm = SCM_UNDEFINED;
    SCM ldflags_scm = SCM_UNDEFINED;
    SCM objects = SCM_UNDEFINED;
    SCM out_scm = SCM_UNDEFINED;

    scm_c_bind_keyword_arguments("link-executable", args, 0, ld_keyword, &ld_scm, ldflags_keyword, &ldflags_scm, 
                            obj_keyword, &objects, target_keyword, &out_scm);

    char *ld;
    char *out;

    char **objs = scm_to_string_list(objects);
    char **ldflags = NULL;

    if (SCM_UNBNDP(ld_scm))
    {
        ld = strdup("ld");
    }
    else  
    {
        ld = scm_to_stringn(ld_scm, NULL, "ascii", SCM_FAILED_CONVERSION_ERROR);
    }

    if (SCM_UNBNDP(out_scm))
    {
        out = strdup("a.out");
    }
    else  
    {
        out = scm_to_stringn(out_scm, NULL, "ascii", SCM_FAILED_CONVERSION_ERROR);
    }

    if (!SCM_UNBNDP(ldflags_scm))
    {
        ldflags = scm_to_string_list(ldflags_scm);
    }

    size_t length = strlen(ld) + strlen(out) + array_length(objs, 1) + array_length(ldflags, 1) + 23;

    char *buf = calloc(length, 1);
    memcpy(buf, ld, strlen(ld));
    strcat(buf, " ");

    for (size_t i = 0; ldflags != NULL && ldflags[i] != NULL; i++)
    {
        strcat(buf, ldflags[i]);
        strcat(buf, " ");
    }
    
    for (size_t i = 0; objs[i] != NULL; i++)
    {
        strcat(buf, objs[i]);
        strcat(buf, " ");
    }

    strcat(buf, "-o");
    strcat(buf, " ");
    strcat(buf, out);
    strcat(buf, ">/dev/null 2>.err");

    if (system(buf) != 0)
    {
        system("less .err");
    }

    system("rm .err");

    scm_str_list_free(objs);
    if (ldflags != NULL)
    {
        scm_str_list_free(ldflags);
    }

    free(ld);
    free(out);
    free(buf);

    return SCM_ELISP_NIL;
}

static SCM find_file_by_ext(SCM dirname_scm, SCM ext_scm)
{
    struct dirent *dir;
    SCM list_src = SCM_LIST0;
    char *dirname = scm_to_stringn(dirname_scm, NULL, "ascii", SCM_FAILED_CONVERSION_ERROR);
    char *ext = scm_to_stringn(ext_scm, NULL, "ascii", SCM_FAILED_CONVERSION_ERROR);
    DIR *d = opendir(dirname);

    if (d)
    {
        while ((dir = readdir(d)) != NULL)
        {
            if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0)
            {
                continue;
            }

            if (dir->d_type == DT_DIR)
            {
                char *path = calloc(strlen(dirname) + strlen(dir->d_name) + 2, 1);
                memcpy(path, dirname, strlen(dirname));
                strcat(path, "/");
                strcat(path, dir->d_name);

                SCM filename = scm_from_stringn(path, strlen(path), "ascii", SCM_FAILED_CONVERSION_ERROR);
                SCM inside = find_file_by_ext(filename, ext_scm);
                SCM argu = scm_list_2(list_src, inside);
                list_src = scm_append(argu);

                free(path);
            }
            else  
            {
                char *point;
                char *path = calloc(strlen(dir->d_name) + strlen(dirname) + 2, 1);
                memcpy(path, dirname, strlen(dirname));
                strcat(path, "/");
                strcat(path, dir->d_name);

                if ((point = strrchr(dir->d_name, '.')) != NULL ) 
                {
                    if (strcmp(point, ext) == 0) 
                    {
                        SCM argu = scm_list_2(list_src, scm_list_1(scm_from_utf8_string(path)));
                        list_src = scm_append(argu);
                    }
                }
            }

        }

        closedir(d);
        return list_src;
    }

    return SCM_ELISP_NIL;
}

static void *register_functions([[maybe_unused]] void *data)
{
    scm_c_define_gsubr("compile-files", 0, 0, 1, &compile_files);
    scm_c_define_gsubr("link-executable", 0, 0, 1, &link_executable);
    scm_c_define_gsubr("find-file-by-ext", 2, 0, 0, &find_file_by_ext);

    return NULL;
}

int main(int argc, char **argv)
{
    scm_with_guile(&register_functions, NULL);

    if (argc == 1)
    {
        scm_c_primitive_load("build.scm");
    }
    else  
    {
        char *tmp = strdup(argv[1]);
        chdir(dirname(tmp));

        scm_c_primitive_load(basename(argv[1]));
        free(tmp);
    }

    return 0;
}