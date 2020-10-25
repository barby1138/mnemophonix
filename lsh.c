#include <stdlib.h>
#include <string.h>
#include "lsh.h"

enum { MAX_TABLE_SIZE = 2000000 };
struct lsh tables[1];
//struct signature_list* table_buckets[N_BUCKETS][MAX_TABLE_SIZE];
struct signature_list table_buckets_pool[N_BUCKETS * MAX_TABLE_SIZE];
int nb_table_buckets_pool_cnt = 0;
int nb_table_buckets_pool_cnt1 = 0;

inline void free_signature_list(struct signature_list* list) {
    struct signature_list* tmp;
    while (list != NULL) {
        tmp = list->next;
        free(list);
        list = tmp;
    }
}

inline void free_hash_tables(struct lsh* tables) {
    for (unsigned int i = 0 ; i < N_BUCKETS ; i++) {
        /*
        for (unsigned int j = 0 ; j < tables->size ; j++) {
            free_signature_list(tables->buckets[i][j]);
        }
        */
        free(tables->buckets[i]);
    }
    //free(tables);
}


inline static unsigned int count_signatures(struct index* database) {
    unsigned int n = 0;
    for (unsigned int i = 0 ; i < database->n_entries ; i++) {
        n += database->entries[i]->signatures->n_signatures;
    }
    return n;
}


inline struct signature_list* new_signature_list(unsigned int entry_index, unsigned int signature_index, struct signature_list* next) {
    /*
    struct signature_list* l = (struct signature_list*)malloc(sizeof(struct signature_list));
    if (l == NULL) {
        return NULL;
    }
    */
   
    if (nb_table_buckets_pool_cnt > N_BUCKETS * MAX_TABLE_SIZE)
    {
        printf("too small table_buckets_pool\n");
        return NULL;
    }
    struct signature_list* l = &table_buckets_pool[nb_table_buckets_pool_cnt];
    nb_table_buckets_pool_cnt++;

    l->entry_index = entry_index;
    l->signature_index = signature_index;
    l->next = next;

    return l;
}

inline static uint32_t get_minhash(uint8_t* hash, int index) {
    int base = index * BYTES_PER_BUCKET_HASH;
    return (hash[base] << 24) | (hash[base + 1] << 16) | (hash[base + 2] << 8) | hash[base + 3];
}


inline struct lsh* create_hash_tables(struct index* database) {
    printf("create_hash_tables\n");

    memset(tables, 0, sizeof(struct lsh));
    //memset(table_buckets, 0, N_BUCKETS * MAX_TABLE_SIZE * sizeof(struct signature_list*));
    memset(table_buckets_pool, 0, N_BUCKETS * MAX_TABLE_SIZE * sizeof(struct signature_list));
    nb_table_buckets_pool_cnt = 0;

    unsigned int total_signatures = count_signatures(database);
    tables->size = total_signatures / 2;
    for (unsigned int i = 0 ; i < N_BUCKETS ; i++) {
        tables->buckets[i] = (struct signature_list**)calloc(tables->size, sizeof(struct signature_list*));
        if (tables->buckets[i] == NULL) {
            free_hash_tables(tables);
            return NULL;
        }
    }

    for (unsigned int i = 0 ; i < database->n_entries ; i++) {
        for (unsigned int j = 0 ; j < database->entries[i]->signatures->n_signatures ; j++) {
            uint8_t* hash = database->entries[i]->signatures->signatures[j].minhash;
            for (unsigned int k = 0 ; k < N_BUCKETS ; k++) {
                uint32_t index = get_minhash(hash, k) % tables->size;
                struct signature_list* tmp = new_signature_list(i, j, tables->buckets[k][index]);
                if (tmp == NULL) {
                    printf("tmp == NULL\n");
                    return NULL;
                }

                tables->buckets[k][index] = tmp;
            }
        }
    }

    printf("good\n");
    nb_table_buckets_pool_cnt1 = nb_table_buckets_pool_cnt;

    return tables;
}


inline int get_matches(struct lsh* tables, uint8_t* hash, struct signature_list* *list) {
    (*list) = NULL;
    int n = 0;

    nb_table_buckets_pool_cnt = nb_table_buckets_pool_cnt1;

    for (unsigned int i = 0 ; i < N_BUCKETS ; i++) {

        uint32_t index = get_minhash(hash, i) % tables->size;

        struct signature_list* tmp = tables->buckets[i][index];

        // Let's add all these matches to our list
        while (tmp != NULL) {
            struct signature_list* new_item = new_signature_list(tmp->entry_index, tmp->signature_index, *list);
            if (new_item == NULL) {
                return MEMORY_ERROR;
            }
            (*list) = new_item;
            n++;

            tmp = tmp->next;
        }
    }

    return n;
}
