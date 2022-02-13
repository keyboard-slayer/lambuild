(define obj (compile-files
    #:src '("src/main.c")
    #:cflags '(
        "-std=gnu2x"
        "-Wall"
        "-Wextra"
        "-Werror"
        "-ggdb"
        "-fsanitize=undefined"
        "-fsanitize=address"
        "-I/usr/include/guile/2.2"
        "-pthread")))

(link-executable
    #:ld "gcc"
    #:ldflags '(
        "-fsanitize=undefined"
        "-fsanitize=address"
        "-lguile-2.2"
        "-lgc"
        "-lm"
    )
    #:objs obj
    #:target "lambuild")