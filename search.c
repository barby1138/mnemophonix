#include <stdlib.h>
#include <string.h>
#include "lsh.h"
#include "search.h"
#include <sys/time.h>

// Checking hashes by buckets of 4 bytes is meant to
// fail fast. This value is the minimum number of bucket
// matches that we require before giving a closer look
// at a potential match
#define MIN_BUCKET_MATCH_FOR_DEEP_CHECK 5

// Once a pair of hashes has at least MIN_BUCKET_MATCH_FOR_DEEP_CHECK
// bucket matches, we want a minimum score to retain this pair
#define MIN_SCORE 50

// Minimum number of full signature matches that an audio sample must have
// with a given database entry to be retained
#define MIN_SIGNATURE_MATCHES 10

// Minimum average score that an audio sample must have
// with a given database entry to be retained
#define MIN_AVERAGE_SCORE 60

enum {
    SEARCH_TO_MS = 15000,
    MAX_DB_ENTRIES = 10000,
    MAX_MATCHES_CNT = 1000000
};

float scores[MAX_DB_ENTRIES];
int n_matches[MAX_DB_ENTRIES];
struct signature_list array[MAX_MATCHES_CNT];

static long time_in_milliseconds() {
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/**
 * Returns the number of bytes that are identical between
 * the given hashes.
 */
static inline unsigned int compare_hashes(uint8_t* hash1, uint8_t* hash2) {
    unsigned int n = 0;
    for (unsigned int i = 0 ; i < SIGNATURE_LENGTH ; i++) {
        if (hash1[i] == hash2[i]) {
            n++;
        }
    }
    return n;
}

static inline  int compare(struct signature_list* a, struct signature_list* b) {
    int diff = a->entry_index - b->entry_index;
    if (diff != 0) {
        return diff;
    }
    return a->signature_index - b->signature_index;
}

static inline  int compare1(struct signature_list a, struct signature_list b) {
    int diff = a.entry_index - b.entry_index;
    if (diff) { return diff; }
    return a.signature_index - b.signature_index;
}

#define SORT_NAME sl
#define SORT_TYPE  struct signature_list
#define SORT_CMP(a, b) compare1(a, b)

#include "sort.h"

int search(struct signatures* sample, struct index* database, struct lsh* lsh) {
    memset(scores, 0, sizeof(float) * database->n_entries);
    memset(n_matches, 0, sizeof(int) * database->n_entries);
    long qs_time = 0;
    long qs_cnt = 0;

    long time_begin = time_in_milliseconds();
    for (unsigned int i = 0 ; i < sample->n_signatures ; i++) {
        // IP
        if (time_in_milliseconds() - time_begin > SEARCH_TO_MS) {
    	    break;
        }

        struct signature_list* list;

        int res = get_matches(lsh, sample->signatures[i].minhash, &list);
        if (res > MAX_MATCHES_CNT) {
            printf("too many matches_cnt %ld\n", res);
            return MEMORY_ERROR;
        }
        if (res == MEMORY_ERROR) {
            return MEMORY_ERROR;
        }

        memset(array, 0, res * sizeof(struct signature_list));

        struct signature_list* tmp = list;
        for (int j = 0 ; j < res ; j++, list = list->next) {
            array[j].entry_index = list->entry_index;
            array[j].signature_index = list->signature_index;
        }
        free_signature_list(tmp);

        long before_qs = time_in_milliseconds();
        qsort(array, res, sizeof(struct signature_list), (int (*)(const void *, const void *)) compare);
        //sl_tim_sort(array, res);
        
        long after_qs = time_in_milliseconds();
        qs_time += (after_qs - before_qs);
        qs_cnt++;

        unsigned int n_identical_matches = 1;
        for (int j = 1 ; j < res ; j++) {
            if (array[j].entry_index == array[j - 1].entry_index
                && array[j].signature_index == array[j - 1].signature_index) {
                    n_identical_matches++;
            } else {
                if (n_identical_matches >= MIN_BUCKET_MATCH_FOR_DEEP_CHECK) {
                    int entry_index = array[j - 1].entry_index;
                    int signature_index = array[j - 1].signature_index;
                    unsigned int score = compare_hashes(database->entries[entry_index]->signatures->signatures[signature_index].minhash,
                                                        sample->signatures[i].minhash);
                    if (score >= MIN_SCORE) {
                        scores[entry_index] += score;
                        n_matches[entry_index]++;
                        
                        // [IP]
                        if (n_matches[entry_index] >= MIN_SIGNATURE_MATCHES && 
                            ((scores[entry_index] / (float)n_matches[entry_index] >= MIN_AVERAGE_SCORE) )) {
                            printf("qs_time %ld\n", qs_time);
                            printf("qs_cnt %ld\n", qs_cnt);
                            printf("sig_matches %d avg_score  %ld %f\n", qs_cnt, scores[entry_index] / (float)n_matches[entry_index]);

                            return entry_index; 
			            }
                        
                    }
                }
                n_identical_matches = 1;
            }
        }
    }

    
    printf("qs_time %ld\n", qs_time);
    printf("qs_cnt %ld\n", qs_cnt);

    // [IP]
    return NO_MATCH_FOUND;
/*    
    int best_match = NO_MATCH_FOUND;
    int best_score = 0;
    for (unsigned int i = 0 ; i < database->n_entries ; i++) {
        float average_score = scores[i] / (float)n_matches[i];
        if (n_matches[i] >= MIN_SIGNATURE_MATCHES && average_score >= MIN_AVERAGE_SCORE) {
            printf("match >>>> i: %d\n", i);

            if (average_score > best_score) {
                best_score = average_score;
                best_match = i;
            }
        }
    }

    printf("qs_time %d\n", qs_time);
    printf("qs_cnt %d\n", qs_cnt);

    return best_match;
*/
}
