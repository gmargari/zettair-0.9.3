/* mlparse_1.c tests for a specific bug in the parser, report by John
 * Yiannis
 *
 * written nml 2004-08-19
 *
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "str.h"
#include "mlparse.h"

int test_file(FILE *fp, int argc, char **argv) {
    /* this data (in two chunks) comes from the TREC GOV collection.
     * Note that theres a docno in buf2, so you can see where it comes
     * from. (buf1 immediately preceeds buf2 in the collection) */
    const char *buf1 = " <!-- tz1* ->\r\n\
</table>\r\n\
\r\n\
<br><hr><br>\r\n\
send me back to the <a href=\"torn.htm\">Indiana County List.</a><br>\r\n\
</center>\r\n\
\r\n\
</body>\r\n\
</html>\r\n\
\n\
</";

    const char *buf2 = "DOC>\n\
<DOC>\n\
<DOCNO>G45-82-1809307</DOCNO>\n\
<DOCHDR>\n\
http://www.fakr.noaa.gov/npfmc/HAPC/hapcdisc.pdf\n\
HTTP/1.1 200 OK\r\n\
Server: Microsoft-IIS/5.0\r\n\
Date: Mon, 04 Feb 2002 00:20:28 GMT\r\n\
Content-Type: application/pdf\r\n\
Accept-Ranges: bytes\r\n\
Last-Modified: Fri, 16 Jun 2000 18:33:26 GMT\r\n\
ETag: \"01fe45bc1d7bf1:8fe\"\r\n\
Content-Length: 30812\r\n\
</DOCHDR>\n\
                                             Discussion Paper";

    struct mlparse parser;

    if (fp && (fp != stdin)) {
        fprintf(stderr, "string library test run with file, "
          "but contains embedded tests (fp %p stdin %p)\n", 
          (void *) fp, (void *) stdin);
        return EXIT_FAILURE;
    }

    if (mlparse_new(&parser, 49, 200)) {
        char buf[52];
        unsigned int len;
        enum mlparse_ret ret;

        parser.next_in = buf1;
        parser.avail_in = str_len(buf1);

        ret = mlparse_parse(&parser, buf, &len, 1);
        assert((ret == MLPARSE_INPUT));
        parser.next_in = buf2;
        parser.avail_in = str_len(buf2);

        ret = mlparse_parse(&parser, buf, &len, 1);
        assert((ret == MLPARSE_WORD));
        buf[len] = '\0';
        assert(!str_cmp(buf, "tz1"));

        ret = mlparse_parse(&parser, buf, &len, 1);
        assert((ret == MLPARSE_TAG));
        buf[len] = '\0';
        assert(!str_cmp(buf, "/table"));

        ret = mlparse_parse(&parser, buf, &len, 1);
        assert((ret == MLPARSE_TAG));
        buf[len] = '\0';
        assert(!str_cmp(buf, "br"));

        ret = mlparse_parse(&parser, buf, &len, 1);
        assert((ret == MLPARSE_TAG));
        buf[len] = '\0';
        assert(!str_cmp(buf, "hr"));

        ret = mlparse_parse(&parser, buf, &len, 1);
        assert((ret == MLPARSE_TAG));
        buf[len] = '\0';
        assert(!str_cmp(buf, "br"));

        ret = mlparse_parse(&parser, buf, &len, 1);
        assert((ret == MLPARSE_WORD));
        buf[len] = '\0';
        assert(!str_cmp(buf, "send"));

        ret = mlparse_parse(&parser, buf, &len, 1);
        assert((ret == MLPARSE_WORD));
        buf[len] = '\0';
        assert(!str_cmp(buf, "me"));

        ret = mlparse_parse(&parser, buf, &len, 1);
        assert((ret == MLPARSE_WORD));
        buf[len] = '\0';
        assert(!str_cmp(buf, "back"));

        ret = mlparse_parse(&parser, buf, &len, 1);
        assert((ret == MLPARSE_WORD));
        buf[len] = '\0';
        assert(!str_cmp(buf, "to"));

        ret = mlparse_parse(&parser, buf, &len, 1);
        assert((ret == MLPARSE_WORD));
        buf[len] = '\0';
        assert(!str_cmp(buf, "the"));

        ret = mlparse_parse(&parser, buf, &len, 1);
        assert((ret == MLPARSE_TAG));
        buf[len] = '\0';
        assert(!str_cmp(buf, "a"));

        ret = mlparse_parse(&parser, buf, &len, 1);
        assert((ret == MLPARSE_PARAM));
        buf[len] = '\0';
        assert(!str_cmp(buf, "href"));

        ret = mlparse_parse(&parser, buf, &len, 1);
        assert((ret == MLPARSE_PARAMVAL));
        buf[len] = '\0';
        assert(!str_cmp(buf, "tornhtm"));

        ret = mlparse_parse(&parser, buf, &len, 1);
        assert((ret == MLPARSE_WORD));
        buf[len] = '\0';
        assert(!str_cmp(buf, "indiana"));

        ret = mlparse_parse(&parser, buf, &len, 1);
        assert((ret == MLPARSE_WORD));
        buf[len] = '\0';
        assert(!str_cmp(buf, "county"));

        ret = mlparse_parse(&parser, buf, &len, 1);
        assert((ret == MLPARSE_WORD));
        buf[len] = '\0';
        assert(!str_cmp(buf, "list"));

        ret = mlparse_parse(&parser, buf, &len, 1);
        assert((ret == MLPARSE_TAG));
        buf[len] = '\0';
        assert(!str_cmp(buf, "/a"));

        ret = mlparse_parse(&parser, buf, &len, 1);
        assert((ret == MLPARSE_TAG));
        buf[len] = '\0';
        assert(!str_cmp(buf, "br"));

        ret = mlparse_parse(&parser, buf, &len, 1);
        assert((ret == MLPARSE_TAG));
        buf[len] = '\0';
        assert(!str_cmp(buf, "/center"));

        ret = mlparse_parse(&parser, buf, &len, 1);
        assert((ret == MLPARSE_TAG));
        buf[len] = '\0';
        assert(!str_cmp(buf, "/body"));

        ret = mlparse_parse(&parser, buf, &len, 1);
        assert((ret == MLPARSE_TAG));
        buf[len] = '\0';
        assert(!str_cmp(buf, "/html"));

        ret = mlparse_parse(&parser, buf, &len, 1);
        if (ret == MLPARSE_TAG && (buf[len] = '\0', 1) && !str_cmp(buf, "/doc")) {
            /* got the correct thing */
            return 1;
        } else {
            /* incorrect */
            buf[len] = '\0';
            fprintf(stderr, "incorrect parsing (got %d, %s)\n", ret, buf);
            assert(0);
            return 0;
        }
    } else {
        fprintf(stderr, "failed to initalise parser\n");
        return 0;
    }

    return 1;
}

