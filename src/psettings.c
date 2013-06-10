/* psettings.c implements an object that encapsulates parser settings,
 * including a convenience method that reads settings from an XML
 * file.
 *
 * written nml 2004-08-09
 *
 */

#include "firstinclude.h"

#include "psettings.h"

#include "chash.h"
#include "error.h"
#include "str.h"
#include "mlparse_wrap.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

enum type_flags {
    TYPE_FLAG_DEFSET = (1 << 0),     /* default was set (not inherited) */
    TYPE_FLAG_SELF_ID = (1 << 1)     /* documents identify themselves */
};

/* structure to represent the parser settings for one mime type */
struct psettings_type {
    struct chash *tags;              /* hashtable of tags to attrs */
    enum psettings_attr def;         /* default attr */
    enum type_flags flags;           /* flags for this type */
};

/* structure to represent all parser settings */
struct psettings {
    struct chash *strings;           /* hashtable of tag strings */
    struct psettings_type *type;     /* per-type array */
    unsigned int types;              /* size of type array */
    enum psettings_attr def;         /* default settings for unrecognised 
                                      * tags */
};

struct psettings *psettings_new(enum psettings_attr def) {
    struct psettings *pset = malloc(sizeof(*pset));

    if (pset 
      && (pset->strings = chash_ptr_new(5, 0.5, 
          (unsigned int (*)(const void *)) str_hash,
          (int (*)(const void *, const void *)) str_cmp))) {

        pset->type = NULL;
        pset->types = 0;
        pset->def = def;
    } else if (pset) {
        if (pset->strings) {
            chash_delete(pset->strings);
        }
        free(pset);
        pset = NULL;
    }

    return pset;
}

static void freestr(void *userdata, const void *key, void **data) {
    free(*data);
}

void psettings_delete(struct psettings *pset) {
    unsigned int i;

    for (i = 0; i < pset->types; i++) {
        chash_delete(pset->type[i].tags);
    }
    if (pset->type) {
        free(pset->type);
    }
    chash_ptr_ptr_foreach(pset->strings, NULL, freestr);
    chash_delete(pset->strings);
    free(pset);
}

static enum psettings_ret psettings_expand(struct psettings *pset, 
  enum mime_types type) {
    void *ptr;
    unsigned int i;

    if ((ptr = realloc(pset->type, 
        sizeof(*pset->type) * (pset->types * 2 + 1)))) {

        i = pset->types;
        pset->type = ptr;
        pset->types = pset->types * 2 + 1;

        for (; i < pset->types; i++) {
            if ((pset->type[i].tags = chash_ptr_new(1, 2.0, 
                (unsigned int (*)(const void *)) str_hash,
                (int (*)(const void *, const void *)) str_cmp))) {

                pset->type[i].def = pset->def;
                pset->type[i].flags = 0;
            } else {
                /* couldn't initialise hashtable */
                pset->types = i;
                if (type < 0 || (unsigned int) type >= pset->types) {
                    return PSETTINGS_ENOMEM;
                }
                break;
            }
        }
    } else {
        return PSETTINGS_ENOMEM;
    }

    return PSETTINGS_OK;
}

enum psettings_ret psettings_set(struct psettings *pset, enum mime_types type,
  const char *tag, enum psettings_attr attr) {
    void **ptr_find;
    unsigned long int *settings;
    char *localtag;
    enum psettings_ret ret;

    assert(type >= 0);
    while ((unsigned int) type >= pset->types) {
        if ((ret = psettings_expand(pset, type)) != PSETTINGS_OK) {
            return ret;
        }
    }
    assert((unsigned int) type < pset->types);

    if (chash_ptr_luint_find(pset->type[type].tags, tag, &settings) 
      == CHASH_OK) {
        /* found an existing entry, modify it */
        *settings = attr;
    } else {
        /* need to insert new entry */
        
        /* find or insert it into the strings table */
        if (chash_ptr_ptr_find(pset->strings, tag, &ptr_find) == CHASH_OK) {
            localtag = *ptr_find;
        } else {
            if ((localtag = str_dup(tag)) 
              && (chash_ptr_ptr_insert(pset->strings, localtag, localtag) 
                  == CHASH_OK)) {
                /* succeeded, do nothing */
            } else {
                if (localtag) {
                    free(localtag);
                }
                return PSETTINGS_ENOMEM;
            }
        }

        if (chash_ptr_luint_insert(pset->type[type].tags, localtag, attr) 
          == CHASH_OK) {

            /* succeeded, finish up below */
        } else {
            return PSETTINGS_ENOMEM;
        }
    }

    /* keep track of whether documents contain an internal identification */
    if (attr & PSETTINGS_ATTR_ID) {
        pset->type[type].flags |= TYPE_FLAG_SELF_ID;
    }

    return PSETTINGS_OK;
}

enum psettings_ret psettings_type_default(struct psettings *pset, 
  enum mime_types type, enum psettings_attr def) {
    enum psettings_ret ret;

    assert(type >= 0);
    while ((unsigned int) type >= pset->types) {
        if ((ret = psettings_expand(pset, type)) != PSETTINGS_OK) {
            return ret;
        }
    }
    assert((unsigned int) type < pset->types);

    pset->type[type].def = def;
    pset->type[type].flags |= TYPE_FLAG_DEFSET;
    return PSETTINGS_OK;
}

enum psettings_attr psettings_find(struct psettings *pset, 
  enum mime_types type, const char *tag) {
    if (type >= 0 && ((unsigned int) type) < pset->types) {
        return psettings_type_find(pset, &pset->type[type], tag);
    } else {
        return pset->def;
    }
}

enum psettings_attr psettings_type_find(struct psettings *pset, 
  struct psettings_type *type, const char *tag) {
    unsigned long int *settings;

    assert(type);

    if (chash_ptr_luint_find(type->tags, tag, &settings) == CHASH_OK) {
        return *settings;
    } else {
        return pset->def;
    }
}

/* get the set of tags for a given mime type */
enum psettings_ret psettings_type_tags(struct psettings *pset, 
  enum mime_types type, struct psettings_type **ptype) {
    if (type >= 0 && ((unsigned int) type) < pset->types) {
        *ptype = &pset->type[type];
        return PSETTINGS_OK;
    } else {
        *ptype = NULL;
        return PSETTINGS_EEXIST;
    }
}

int psettings_self_id(struct psettings *pset, enum mime_types type) {
    if (type >= 0 && ((unsigned int) type) < pset->types) {
        return pset->type[type].flags & TYPE_FLAG_SELF_ID;
    } else {
        return 0;
    }
}

#define WORDLEN 100

static int parse(struct mlparse_wrap *parser, char *buf, unsigned int *len, 
  int strip) {
    enum mlparse_ret ret;

    do {
        ret = mlparse_wrap_parse(parser, buf, len, strip);

        /* filter out comments and whitespace and cdata sections */
        switch (ret) {
        case MLPARSE_COMMENT:
            /* skip to end of comment */
            do {
                ret = parse(parser, buf, len, strip);

                switch (ret) {
                case MLPARSE_WORD:
                case MLPARSE_END | MLPARSE_WORD:
                case MLPARSE_CONT | MLPARSE_WORD:
                    break;

                case MLPARSE_END | MLPARSE_COMMENT:
                    break;

                case MLPARSE_EOF:
                    return MLPARSE_EOF;

                default: 
                    assert(0);
                    return MLPARSE_ERR;
                };

            } while ((ret != (MLPARSE_END | MLPARSE_COMMENT)));
            break;

        case MLPARSE_WHITESPACE:
            break;

        case MLPARSE_CDATA:
        case MLPARSE_END | MLPARSE_CDATA:
            break;

        default: 
            return ret;
        };
    } while (1);
}

static int psettings_read_tag(struct psettings *pset, 
  struct mlparse_wrap *parser, enum mime_types type, char *buf, char *tagbuf) {
    enum mlparse_ret ret;
    unsigned int len;
    enum psettings_attr attr = PSETTINGS_ATTR_INDEX,
                        ex_attr;            /* existing attribute */
    struct psettings_type *ptype;

    /* 'tag' tag can contain name and index attributes only */

    /* read name and index tags */
    tagbuf[0] = '\0';
    while ((ret = parse(parser, buf, &len, 1)) == MLPARSE_PARAM) {
        buf[len] = '\0';
        if (!str_casecmp(buf, "name")) {
            assert(tagbuf[0] == '\0');
            if ((ret = parse(parser, tagbuf, &len, 0)) == MLPARSE_PARAMVAL) {
                /* got the tag name in tagbuf */
                tagbuf[len] = '\0';
                str_tolower(tagbuf);
            } else {
                ERROR("expecting parameter value after name attribute");
                return 0;
            }
        } else if (!str_casecmp(buf, "index")) {
            if ((ret = parse(parser, buf, &len, 1)) == MLPARSE_PARAMVAL) {
                buf[len] = '\0';
                if (!str_casecmp(buf, "true")) {
                    attr |= PSETTINGS_ATTR_INDEX;
                } else if (!str_casecmp(buf, "false")) {
                    attr &= ~PSETTINGS_ATTR_INDEX;
                } else if (!str_casecmp(buf, "binary")) {
                    attr &= ~PSETTINGS_ATTR_INDEX;
                    attr |= PSETTINGS_ATTR_CHECKBIN;
                } else if (!str_casecmp(buf, "pop")) {
                    attr &= ~PSETTINGS_ATTR_INDEX;
                    attr |= PSETTINGS_ATTR_POP;
                } else if (!str_casecmp(buf, "noop")) {
                    attr &= ~PSETTINGS_ATTR_INDEX;
                    attr |= PSETTINGS_ATTR_NOOP;
                } else if (!str_casecmp(buf, "identifier")) {
                    attr &= ~PSETTINGS_ATTR_INDEX;
                    attr |= PSETTINGS_ATTR_ID;
                } else if (!str_casecmp(buf, "offend")) {
                    attr &= ~PSETTINGS_ATTR_INDEX;
                    attr |= PSETTINGS_ATTR_OFF_END;
                } else if (!str_casecmp(buf, "title")) {
                    attr &= ~PSETTINGS_ATTR_INDEX;
                    attr |= PSETTINGS_ATTR_TITLE;
                } else {
                    ERROR("expected true, false, binary or identifier as "
                      "value for index attribute");
                    return 0;
                }
            } else {
                ERROR("expecting parameter value after index attribute");
                return 0;
            }
        } else if (!str_casecmp(buf, "flow")) {
            if ((ret = parse(parser, buf, &len, 1)) == MLPARSE_PARAMVAL) {
                buf[len] = '\0';
                if (!str_casecmp(buf, "true")) {
                    attr |= PSETTINGS_ATTR_FLOW;
                } else if (!str_casecmp(buf, "false")) {
                    /* do nothing */
                } else {
                    ERROR("expected true or false as value for flow "
                      "attribute");
                    return 0;
                }
            } else {
                ERROR("expecting parameter value after flow attribute");
                return 0;
            }
        } else {
            ERROR("expected name, index or flow attribute");
            return 0;
        }
    }

    /* sanity checks */
    if (!tagbuf[0] || ret != MLPARSE_TAG 
      || ((buf[len] = '\0'), str_cmp(buf, "/tag"))) {
        if (!tagbuf[0]) {
            ERROR("name attribute required");
        } else {
            ERROR("expected '/tag' tag");
        }
        return 0;
    }

    /* insert settings for this tag into the table, making sure we don't
     * overwrite endtag for this tag if it is set */
    if (psettings_type_tags(pset, type, &ptype) == PSETTINGS_OK) {
        ex_attr = psettings_type_find(pset, ptype, tagbuf);
        attr |= ex_attr & PSETTINGS_ATTR_DOCEND;
        if (psettings_set(pset, type, tagbuf, attr) == PSETTINGS_OK) {
            /* tag settings created */
            return 1;
        } else {
            ERROR1("couldn't insert tag %s", tagbuf);
            return 0;
        }
    } else {
        assert("can't get here" && 0);
        return 0;
    }
}

static int psettings_read_type(struct psettings *pset, 
  struct mlparse_wrap *parser, char *buf, char *tagbuf) {
    char *endtag = NULL;
    enum mime_types type = MIME_TYPE_UNKNOWN_UNKNOWN;
    enum psettings_attr def = PSETTINGS_ATTR_INDEX;
    enum mlparse_ret ret;
    unsigned int len;

    /* 'type' tag contains mimetype (required), endtag (optional) and default
     * (optional) attributes, followed by a list of 'tag' tags */
    while ((ret = parse(parser, buf, &len, 1)) == MLPARSE_PARAM) {
        buf[len] = '\0';
        if (!str_cmp(buf, "mimetype")) {
            if ((ret = parse(parser, buf, &len, 0)) == MLPARSE_PARAMVAL) {
                /* save name */
                buf[len] = '\0';
                str_tolower(buf);
                type = mime_type(buf);
            } else {
                ERROR("expected parameter value for mimetype attribute");
                if (endtag) free(endtag);
                return 0;
            }
        } else if (!str_cmp(buf, "endtag")) {
            if ((ret = parse(parser, buf, &len, 0)) == MLPARSE_PARAMVAL) {
                /* save name */
                buf[len] = '\0';
                str_tolower(buf);
                if (!(endtag = str_dup(buf))) {
                    ERROR1("no memory to copy '%s' endtag", buf);
                    if (endtag) free(endtag);
                    return 0;
                }
            } else {
                ERROR("expected parameter value for mimetype attribute");
                if (endtag) free(endtag);
                return 0;
            }
        } else if (!str_cmp(buf, "default")) {
            if ((ret = parse(parser, buf, &len, 1)) == MLPARSE_PARAMVAL) {
                buf[len] = '\0';
                str_tolower(buf);
                if (!str_cmp(buf, "true")) {
                    def = PSETTINGS_ATTR_INDEX;
                } else if (!str_cmp(buf, "false")) {
                    def = 0;
                } else {
                    ERROR1("expected true or false value for default, got '%s'",
                      buf);
                    if (endtag) free(endtag);
                    return 0;
                }
            } else {
                ERROR("expected parameter value for mimetype attribute");
                if (endtag) free(endtag);
                return 0;
            }
        } else {
            ERROR1("unrecognised attribute '%s' in 'type' tag", buf);
            if (endtag) free(endtag);
            return 0;
        }
    }

    if (type == MIME_TYPE_UNKNOWN_UNKNOWN) {
        ERROR("didn't receive mimetype for 'type' tag");
        if (endtag) free(endtag);
        return 0;
    }

    /* insert type and default into psettings */
    if (psettings_type_default(pset, type, def) == PSETTINGS_OK) {
        /* insert endtag tag if received */
        if (endtag) {
            /* note that enddoc tag gets DOCEND attribute only */
            def = PSETTINGS_ATTR_DOCEND;
            if (psettings_set(pset, type, endtag, def) != PSETTINGS_OK) {
                ERROR("couldn't set endtag");
                free(endtag);
                return 0;
            }
            free(endtag);
        }
    } else {
        ERROR("couldn't insert new mime type");
        if (endtag) free(endtag);
        return 0;
    }

    /* process list of 'tag' tags */
    while ((ret == MLPARSE_TAG)
      && !str_nncmp(buf, len, "tag", str_len("tag"))) {
        if (!psettings_read_tag(pset, parser, type, buf, tagbuf)) {
            return 0;
        }
        ret = parse(parser, buf, &len, 1);
    }

    if (ret == MLPARSE_TAG && !str_nncmp(buf, len, "/type", str_len("/type"))) {
        /* succeeded */
        return 1;
    } else {
        ERROR("unexpected ending of 'type' tag");
        return 0;
    }
}

struct psettings *psettings_read(FILE *fp, enum psettings_attr def) {
    struct psettings *pset;
    struct mlparse_wrap *parser;
    char buf[WORDLEN + 1];
    char tagbuf[WORDLEN + 1];
    unsigned int len;
    enum mlparse_ret ret;

    if ((pset = psettings_new(def)) 
      && (parser = mlparse_wrap_new_file(WORDLEN, 1024, fp, 1024, 0))) {

        /* read initial junk (xml declaration) */
        do {
            ret = parse(parser, buf, &len, 1);

            switch (ret) {
            case MLPARSE_TAG:
                if (!str_cmp(buf, "?xml")) {
                    break;
                }
                /* otherwise fallthrough to error handling */

                /* only allow tags, comments whitespace at the start */
            default:
                mlparse_wrap_delete(parser);
                psettings_delete(pset);
                return NULL;
            }
        } while (!((ret == MLPARSE_TAG) && !str_cmp(buf, "?xml")));

        /* read initial junk (rest of xml declaration, psettings tag) */
        do {
            ret = parse(parser, buf, &len, 1);

            switch (ret) {
            case MLPARSE_PARAM:
            case MLPARSE_PARAMVAL:
                break;

            case MLPARSE_TAG:
                if (!str_cmp(buf, "psettings")) {
                    break;
                }
                /* otherwise fallthrough to error handling */

                /* only allow tags, whitespace at the start */
            default:
                mlparse_wrap_delete(parser);
                psettings_delete(pset);
                return NULL;
            }
        } while (!((ret == MLPARSE_TAG) && !str_cmp(buf, "psettings")));

        /* read types */
        do {
            ret = parse(parser, buf, &len, 1);

            switch (ret) {
            case MLPARSE_TAG:
                buf[len] = '\0';
                if (!str_cmp(buf, "type")) {
                    if (!psettings_read_type(pset, parser, buf, tagbuf)) {
                        mlparse_wrap_delete(parser);
                        psettings_delete(pset);
                        return NULL;
                    }
                    break;
                } else if (!str_cmp(buf, "/psettings")) {
                    break;
                }
                /* otherwise fallthrough to error handling */

                /* only allow tags, whitespace at the start */
            default:
                mlparse_wrap_delete(parser);
                psettings_delete(pset);
                return NULL;
            }
        } while (!((ret == MLPARSE_TAG) && !str_cmp(buf, "/psettings")));

        /* have read closing junk, now finish file */
        do {
            ret = parse(parser, buf, &len, 1);

            switch (ret) {
            case MLPARSE_EOF:
                break;

            default:
                mlparse_wrap_delete(parser);
                psettings_delete(pset);
                return NULL;
            }
        } while (ret != MLPARSE_EOF);

        mlparse_wrap_delete(parser);
        return pset;
    } else {
        if (pset) {
            psettings_delete(pset);
        }
        return NULL;
    }
}

struct tmptag {
    char *name;
    unsigned int settings;
};

int tmptag_cmp(const void *vone, const void *vtwo) {
    const struct tmptag *one = vone,
                        *two = vtwo;

    return str_cmp(one->name, two->name);
}

enum psettings_attr psettings_default(struct psettings *pset) {
    return pset->def;
}

enum psettings_ret psettings_write_code(struct psettings *pset, FILE *output) {
    unsigned int i;
    struct chash_iter *iter;

    fprintf(output, "/* code generated by psettings_write_code() to produce a "
      "psettings object with \n * default settings */\n\n");

    fprintf(output, "#include \"firstinclude.h\"\n");
    fprintf(output, "#include <assert.h>\n");
    fprintf(output, "#include <stdlib.h>\n\n");
    fprintf(output, "#include \"psettings.h\"\n\n");

    fprintf(output, "struct psettings *psettings_new_default(enum "
      "psettings_attr def) {\n");
    fprintf(output, "    struct psettings *pset = psettings_new(def);\n");
    fprintf(output, "    enum psettings_ret ret;\n\n");
    fprintf(output, "    if (pset) {\n");

    /* load index names in object, by iterating over names hashtable.  Types
     * will have the same numbers as they currently do */

    /* iterate over types */
    for (i = 0; i < pset->types; i++) {
        struct chash *curr = pset->type[i].tags;
        struct tmptag *tags = NULL;

        if (chash_size(curr) || (pset->type[i].flags & TYPE_FLAG_DEFSET)) {
            fprintf(output, "        if ((ret = psettings_type_default(pset, "
              "%u, %u))\n", i, pset->type[i].def);
            fprintf(output, "          != PSETTINGS_OK) {\n");
            fprintf(output, "            psettings_delete(pset);\n");
            fprintf(output, "            return NULL;\n");
            fprintf(output, "        }\n");

            if ((iter = chash_iter_new(curr)) 
              && (tags = malloc(sizeof(*tags) * chash_size(curr)))) {
                unsigned int j;
                const void *key;
                unsigned long int *data;

                j = 0;
                while (chash_iter_ptr_luint_next(iter, &key, &data) 
                  == CHASH_OK) {
                    assert(j < chash_size(curr));
                    tags[j].name = (char *) key;
                    tags[j++].settings = *data;
                }

                qsort(tags, chash_size(curr), sizeof(*tags), tmptag_cmp);

                for (j = 0; j < chash_size(curr); j++) {
                    fprintf(output, "        if ((ret = psettings_set(pset, "
                      "%u, \"%s\", %u))\n", 
                      i, tags[j].name, tags[j].settings);
                    fprintf(output, "          != PSETTINGS_OK) {\n");
                    fprintf(output, "            psettings_delete(pset);\n");
                    fprintf(output, "            return NULL;\n");
                    fprintf(output, "        }\n");
                }

                free(tags);
                chash_iter_delete(iter);
            } else {
                if (iter) {
                    chash_iter_delete(iter);
                }
                return PSETTINGS_ENOMEM;
            }
        }
        /* otherwise its empty, just ignore it */
    }

    fprintf(output, "    }\n\n");
    fprintf(output, "    return pset;\n");
    fprintf(output, "}\n\n");

    fprintf(output, "#ifdef PSETTINGS_DEFAULT_TEST\n\n");

    fprintf(output, "int main() {\n");
    fprintf(output, "    struct psettings *pset "
      "= psettings_new_default(PSETTINGS_ATTR_INDEX);\n");
    fprintf(output, "\n");

    fprintf(output, "    psettings_write_code(pset, stdout);\n\n");

    fprintf(output, "    psettings_delete(pset);\n");
    fprintf(output, "    return EXIT_SUCCESS;\n\n");
    fprintf(output, "}\n\n");

    fprintf(output, "#endif\n\n");

    return PSETTINGS_OK;
}

#ifdef PSETTINGS_MAIN

int main(int argc, char **argv) {
    struct psettings *pset;
    FILE *fp,
         *outfp;

    if (argc > 3) {
        fprintf(stderr, "usage: %s [file] [outfile]\n", *argv);
        return EXIT_FAILURE;
    }

    if (argc > 2) {
        outfp = fopen(argv[2], "wb");

        if (!outfp) {
            perror(*argv);
            return EXIT_FAILURE;
        }
    } else {
        outfp = stdout;
    }

    if (argc > 1) {
        fp = fopen(argv[1], "rb");

        if (!fp) {
            perror(*argv);
            fclose(outfp);
            return EXIT_FAILURE;
        }
    } else {
        fp = stdin;
    }

    if ((pset = psettings_read(fp, PSETTINGS_ATTR_INDEX))) {
        fclose(fp);
        psettings_write_code(pset, outfp);
        fclose(outfp);
        psettings_delete(pset);
        return EXIT_SUCCESS;
    } else {
        fclose(fp);
        fclose(outfp);
        fprintf(stderr, "failed to create psettings object from stream\n");
        return EXIT_FAILURE;
    }
}

#endif

