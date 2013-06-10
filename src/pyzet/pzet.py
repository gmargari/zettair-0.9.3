import zet
import string
import tempfile
import os
import sys
import copy_reg
import re

# register de-pickler
copy_reg.constructor(zet.unpickle_search_result)

class ZetIndex(zet.Index):
    """Extends C-wrapper index."""

    DEFAULT_LEN=20

    def __init__(self, prefix="index"):
        zet.Index.__init__(self, prefix)
        self.prefix = prefix

    def search(self, query, *args, **kys):
        baseResults = zet.Index.search(self, query, *args, **kys)
        return ZetSearchResults(list(baseResults.results), 
                baseResults.total_results)

    def trec_search(self, trec_query, len, *args, **kys):
        return self.search(trec_query.query, 0, 
                len, *args, **kys).to_trec_eval_list(trec_query.topic_num)

    def __getitem__(self, key):
        return self.search(key)

class TrecResult:
    """Data for single line of a trec result.
    This contains the topic number, trec docid, score, and run-id."""

    def __init__(self, topic_number, trec_doc_id, score=0.0, run_id="zettair"):
        self.topic_number = topic_number
        self.trec_doc_id = trec_doc_id
        self.score = score
        self.run_id = run_id

    def fmt(self):
        """Return as formatted string in trec_eval format"""
        return "%s\tQ0\t%s\t0\t%f\t%s" % (self.topic_number, 
                self.trec_doc_id, self.score, self.run_id)

class ZetSearchResults:
    """Wrapper for C-module SearchResults."""

    def __init__(self, results, total_results):
        self.results = results
        self.total_results = total_results

    def __iter__(self):
        return self.results.__iter__()

    def __getitem__(self, index):
        return self.results[index]

    def add_results(self, results):
        self.results = self.results + results

    def to_trec_eval_list(self, topic_num, run_id="zettair"):
        "Convert to a list of TrecResults"
        trec_results = []
        for result in self.results:
            trec_results.append(TrecResult(topic_num, result.auxiliary,
                    result.score, run_id))
        return trec_results

    def order_by_score(self):
        """Order results by score."""
        self.results.sort(lambda r1, r2: int((r2.score - r1.score) * 1000))

    def order_by_auxiliary(self):
        """Order results by auxiliary field."""
        self.results.sort(lambda r1, r2: cmp(r1.auxiliary, r2.auxiliary))

class Query:

    def __init__(self, query):
        self.query = query
        self.terms = None

class TrecQuery(Query):
    """A query annotated with a topic number, as per TREC."""

    def __init__(self, topic_num, query):
        Query.__init__(self, query)
        self.topic_num = topic_num

    def __str__(self):
        return "topic# %s, query \"%s\"" % (self.topic_num, self.query)

    def __repr__(self):
        return "<pzet.TrecQuery:: topic# %s, query \"%s\">" \
                % (self.topic_num, self.query)

class TrecEvalResult:
    """Result of running trec eval over the output of a zet_trec run"""

    # currently we only capture total precision
    def __init__(self, average_precision, r_precision,
            at_docs_precision):
        self.average_precision = average_precision
        self.r_precision = r_precision
        self.at_docs_precision = at_docs_precision

class ZetMLParser(zet.MLParser):
    """Extends MLParser.
    Brings MLParse token identifiers into Python."""

    # token types, copied from mlparse.h

    TAG = 2 << 2
    WORD = 3 << 2
    PARAM = 4 << 2
    PARAMVAL = 5 << 2
    COMMENT = 6 << 2
    WHITESPACE = 7 << 2
    CDATA = 8 << 2

class CachedPostingsIterator:
    """Wrapper for postings iterator.
    Stores current posting in current_posting field."""

    def __init__(self, postings_iterator):
        self.postings_iterator = postings_iterator
        self.current_posting = None

    def __iter__(self):
        return self

    def next(self):
        self.current_posting = self.postings_iterator.next()
        return self.current_posting

    def skip_to(self, docno):
        self.postings_iterator.skip_to(docno)

def parse_trec_eval_output(trec_eval_proc):
    """Parse the output from a run of trec_eval into a TrecEvalResult object"""
    state="START"
    at_re = re.compile("  At *(\d+) docs: *(\d+\.\d+)")
    at_docs_precision = {}
    for line in trec_eval_proc:
        if state == "START":
            if line.startswith("Average precision"):
                state = "AVERAGE"
        elif state == "AVERAGE":
            average_precision = float(line)
            state = "AFTER AVERAGE"
        elif state == "AFTER AVERAGE":
            mo = at_re.match(line)
            if mo != None:
                docs = int(mo.group(1))
                precision = float(mo.group(2))
                at_docs_precision[docs] = precision
            elif line.startswith("R-Precision"):
                state = "R-PRECISION"
        elif state == "R-PRECISION":
            r_precision = float(line.split()[1])
            state = "END"
        else:
            raise StandardError, "invalid parse state"
    if (state != "END"):
        raise StandardError, "didn't reach end"
    return TrecEvalResult(average_precision, r_precision, at_docs_precision)

# FIXME when called, should normally pass in wordlen, lookahead
# used to build index.
def extract_words(buf, limit=-1, wordlen=49, lookahead=999):
    return zet.extract_words(buf, limit, wordlen, lookahead)

def trec_queries_from_short_topic_file(filename):
    trec_queries = []
    fp = open(filename)
    for line in fp:
        (topicnum, query) = string.split(line.rstrip(), " ", 1)
        trec_query = TrecQuery(topicnum, query)
        trec_queries.append(trec_query)
    fp.close()
    return trec_queries

def trec_results_from_trec_query_list(index, queries, len, *args, **kys):
    results = []
    for tq in queries:
        result = index.trec_search(tq, len, *args, **kys)
        results.append(result)
    return results

def trec_results_from_short_topic_file(index, filename, len, *args, **kys):
    """Get a nested list of trec results for a topic file.
    The outer list contains the results for each topic.
    Each inner list contains TrecResult objects."""
    queries = trec_queries_from_short_topic_file(filename)
    return trec_results_from_trec_query_list(index, queries, len,
            *args, **kys)

def trec_eval_results(results, qrels, trec_eval_cmd="trec_eval", 
        results_fn=tempfile.mktemp()):
    """Execute trec_eval on the results of a trec run."""
    results_fp = open(results_fn, 'w')
    for result_set in results:
        results_fp.writelines(map(lambda x: x.fmt() +"\n", result_set))
    results_fp.close()
    trec_eval_proc = os.popen("%s %s %s" % (trec_eval_cmd, 
        qrels, results_fn))
    trec_eval_out = parse_trec_eval_output(trec_eval_proc)
    trec_eval_proc.close()
    return trec_eval_out
