
#include <stdbool.h>

// Hash table interface
typedef struct _hash_table hash_table_t;

// key is a void *, data is a void *
// key and data must not change after being added to the table
// The hashing function must be provided by the caller
typedef size_t (*hash_fct)( void *key );

hash_table_t * new_hash_table( size_t n_entries, hash_fct hash_data );

bool hash_table_add( hash_table_t *ht, void *key, void *data );
bool hash_table_remove( hash_table_t *ht, void *key );
void *hash_table_lookup( hash_table_t *ht, void *key );

void debug_hash_table( hash_table_t *ht );

// visit entries in hash table - multiple entries with the same
// hash value are always visited in sequence (change in hash
// value indicates the end of a sequence).
typedef void (*visit_fct)( size_t hash, int remaining,
                           void *key, void *data, void *private );

// traverse hash table
void hash_table_traverse( hash_table_t *ht, visit_fct visit, void *private );

