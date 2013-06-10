/* file.c implements file diagnosis through the mime facility of
 * the search engine
 *
 * written nml 2004-06-11
 *
 */

#include "firstinclude.h"

#include "mime.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    FILE *fp;
    char buf[BUFSIZ];
    unsigned int i,
                 len;
    enum mime_types type;

    if (argc < 2) {
        printf("usage: %s file+\n", *argv);
        return EXIT_SUCCESS;
    }

    for (i = 1; i < argc; i++) {
        if ((fp = fopen(argv[i], "rb"))) {
            rewind(fp);
            if ((len = fread(buf, 1, BUFSIZ, fp)) || !ferror(fp)) {
                type = mime_content_guess(buf, len);
                printf("%s: %s\n", argv[i], mime_string(type));
            } else {
                perror(*argv);
                fclose(fp);
                return EXIT_FAILURE;
            }
        } else {
            perror(*argv);
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}


