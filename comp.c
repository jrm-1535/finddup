
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <assert.h>
#include <magic.h>

#ifdef TIME_MEASURE
#include <time.h>   // only for timing measurements
#endif

#include "comp.h"

#ifdef TIME_MEASURE
#define SEC_TO_NANOSEC(s)       ((s)*1000000000)
#define NANOSEC_TO_MILLISEC(n)  ((n)/1000000)
static int64_t get_nanosecond_timestamp( void )
{
    uint64_t nanoseconds;
    struct timespec ts;
    if ( -1 == clock_gettime(CLOCK_MONOTONIC_RAW, &ts) ) {
        printf("Failed to obtain timestamp. errno = %i: %s\n", errno,
               strerror(errno));
        nanoseconds = UINT64_MAX; // use this to indicate error
    } else {
        nanoseconds = SEC_TO_NANOSEC((uint64_t)ts.tv_sec) + (uint64_t)ts.tv_nsec;
    }
    return nanoseconds;
}
#endif

static void *malloc_or_exit( size_t size )
{
    void *d = malloc( size );
    if ( NULL == d ) {
        exit( NO_MEMORY_ERROR );
    }
    return d;
}

// return true if path is NOT used for other purpose, allowing it to be freed
typedef bool (*process_file_t)( char *path, size_t size, void *context );

static void traverse_directory( char *path, bool nosub,
                                process_file_t process, void *ctxt)
{
//    printf( "Entering directory %s\n", path );
    DIR *ref_dir = opendir( path );
    if ( NULL == ref_dir ) {
        printf( "Unable to open directory %s (errno %d) - exiting\n", path, errno );
        exit(FILE_IO_ERROR);
    }

    while ( true ) {
        struct dirent *ref_de = readdir( ref_dir );
        if ( NULL == ref_de ) {
            break;
        }

        unsigned char ref_detype = ref_de->d_type;
        char * ref_dename = ref_de->d_name;
        if ( ref_dename[0] == '.' ) {
            if ( ref_dename[1] == 0 || ( ref_dename[1] == '.' && ref_dename[2] == 0 ) )
                continue;       // skip parent and current directory
        }

        int cur_path_size = strlen(path);
        char *new_path = malloc_or_exit( cur_path_size + strlen(ref_dename) + 2 );
        strcpy( new_path, path );
        if ( '/' != path[cur_path_size-1]) {
            new_path[cur_path_size] = '/';
            ++cur_path_size;
        }
        strcpy( new_path + cur_path_size, ref_dename );
        struct stat stat_data;
        int res;

        switch ( ref_detype ) {
        case DT_REG:
            res = stat( new_path, &stat_data );
            if ( res != 0 ) {
                printf( "unable to stat regular file %s\n", new_path );
                exit(FILE_IO_ERROR);
            }
//            printf( "size %ld, path %s\n", stat_data.st_size, new_path );
            if ( process( new_path, stat_data.st_size, ctxt ) ) {
                free( new_path );
            }
            break;
        case DT_DIR:
            if ( ! nosub ) {
                traverse_directory( new_path, nosub, process, ctxt );
            }
            free( new_path );
            break;
        default:
            printf( "Skipping special file %s\n", new_path );
            free( new_path );
            break;
        }
    }
    closedir( ref_dir );
}

#define MAX_STATIC_BUFFER_SIZE  (2 * 1024 * 1024 )
static bool bin_compare( FILE *f1, FILE *f2 )
{
    static char buffer1[ MAX_STATIC_BUFFER_SIZE ];
    static char buffer2[ MAX_STATIC_BUFFER_SIZE ];

    fseek( f1, 0, SEEK_SET );       // f1 may be used multiple times
    size_t n1 = fread( buffer1, 1, MAX_STATIC_BUFFER_SIZE, f1 );
    size_t n2 = fread( buffer2, 1, MAX_STATIC_BUFFER_SIZE, f2 );
    if ( n1 != n2 ) return false;   // should never happen, sizes are the same

    for ( size_t i = 0; i < n1; ++ i ) {
        if ( buffer1[i] != buffer2[i] )
            return false;
    }
    return true;
}

/*
    Use a simple map with file size as key. Since multiple files can have
    the same size, the data is linked list of file paths with the same size.

    key = size -> data = name_list_t of names allocated in memory
*/

// used for storing list of files with the same size
// The prev list is circular with the head item pointing to the last one
// this allow O(0) access to the last item during creation
typedef struct _name_list {
    struct _name_list   *next;  // regular linked list ending with NULL
    struct _name_list   *prev;  // circular linked list during creation
    char                *name;
} name_list_t;

// once created the orignal list is never directly modified. Instead, each
// time lists need modification, they are first duplicated. After duplication
// the prev linked list is not circular anymore (head->prev is NULL)
static name_list_t *duplicate_list( const name_list_t *l )
{
    name_list_t *dl, *p;
    dl = p = NULL;
    for ( const name_list_t *item = l; NULL != item; item = item->next ) {
        name_list_t *d = malloc_or_exit( sizeof( name_list_t ) );
        d->name = item->name;
        d->prev = p;
        d->next = NULL;
        if ( NULL == dl ) {
            dl = d;
        } else {
            p->next = d;
        }
        p = d;
    }
    return dl;
}

// shallow free, does not free the file name here (still in use in the
// original list)
static void free_duplicate_list( name_list_t *l)
{
    name_list_t *entry = l;
    while ( NULL != entry ) {
        name_list_t *to_remove = entry;
        entry = entry->next;
        free( to_remove );
    }
}

static magic_t open_magic_lib( void )
{
	static magic_t magic_cookie;

	/*MAGIC_MIME tells magic to return a mime of the file, but you can specify different things*/
	magic_cookie = magic_open(MAGIC_MIME);
	if (magic_cookie == NULL) {
        printf("unable to initialize magic library\n");
        exit( INTERNAL_ERROR );
	}
//    printf("Loading default magic database\n");
    if (magic_load(magic_cookie, NULL) != 0) {
        printf("cannot load magic database - %s\n", magic_error(magic_cookie));
        magic_close(magic_cookie);
        exit( INTERNAL_ERROR );
    }
    return magic_cookie;
}

static void close_magic_lib( magic_t magic_cookie )
{
	magic_close(magic_cookie);
}

static size_t get_escaped_path_len( const char * path )
{
    // shells requie to escape some characters, such as '('
    size_t sz = 0;
    while ( 0 != *path ) {
        switch( *path++ ) {
        case '(': case ')': case '\'': case ';':
            sz += 2;
            break;
        default:
            ++sz;
            break;
        }
    }
    return sz;
}

static bool write_escaped_path( char *buffer, size_t len, const char *path )
{
    size_t i = 0;
    while ( 0 != *path && len > 1) {
        switch( *path ) {
        case '(': case ')': case '\'': case ';':
            buffer[i++] = '\\';
            break;
        default:
            break;
        }
        buffer[i++] = *path;
        ++path;
    }
    if ( 0 != *path ) {
        return false;\
    }
    buffer[i] = '\0';
    return true;
}

static void run_viewer( const char *viewer, const char * path )
{
    size_t vlen = strlen( viewer );
    size_t plen = get_escaped_path_len( path );
    size_t sz = vlen + plen + 2;
    char *buffer = malloc_or_exit( sz );
    int n = snprintf( buffer, sz, "%s ", viewer );
    if ( n == (int)(vlen + 1) && write_escaped_path( &buffer[n], 1 + plen, path ) ) {
//        printf( "command %s\n", buffer );
        printf( "It may take a while to view the file - Exit viewer to continue\n" );
        system( buffer );
    }
    free( buffer );
}

static void view_file( magic_t cookie, const char *path )
{
    const char *desc = magic_file( cookie, path);
//	printf("%s\n", desc);
    const char *known_mime_types[] =
        { "image/bmp", "image/gif;", "image/jpeg", "image/png",
          "image/apng", "image/tiff",
          "text/plain", "text/csv", "text/xml", "application/xml",
          "application/json", "text/html",
           "application/pdf" };
    const char *default_viewer[] =
        { IMAGE_VIEWER, IMAGE_VIEWER, IMAGE_VIEWER, IMAGE_VIEWER,
          IMAGE_VIEWER, IMAGE_VIEWER,
          TEXT_VIEWER, TEXT_VIEWER, TEXT_VIEWER, TEXT_VIEWER,
          TEXT_VIEWER, TEXT_VIEWER,
          PDF_VIEWER
        };
    for ( size_t i = 0; i < sizeof( known_mime_types ) / sizeof( char * ); ++i ) {
        if ( 0 == strncmp( desc, known_mime_types[i],
                          strlen( known_mime_types[i] ) ) ) {
//            printf( "Viewer: %s\n", default_viewer[i] );
            run_viewer( default_viewer[i], path );
            return;
        }
    }
    printf( "No viewer defined for %s\n", desc );
}

typedef struct {
    const char  *path;
    map_t       *map;
    magic_t     cookie;
    size_t      redundant;
    bool        compare;
    bool        remove;
    bool        confirm;
    bool        zero;

} target_context_t;

// return true to stop immediately, false to keep processing files
static bool interactive_remove_files( target_context_t *tc,
                                      const name_list_t *names, int nnames )
{
    while ( true ) {
        printf( "> Enter v to view content, x to exit, or a space separated "
                "list of name indexes to remove or nothing to skip removing: " );
        fflush( stdout );
        char *buffer = malloc_or_exit( 20 * nnames );   // some extra space
        int n = read( fileno(stdin), buffer, 20 * nnames );
        if ( n > 1 ) {
            char **to_remove = malloc_or_exit( sizeof(char *) * (nnames + 1) );
            char *end = buffer;
            int  k = 0;
            --n;

            while ( end - buffer < n ) {
                while ( *end == ' ' && end - buffer < n ) ++end;
                if ( 'x' == *end || 'X' == *end ) {
                    return true;
                }
                if ( 'v' == *end || 'V' == *end ) {
                    view_file( tc->cookie, names[0].name );
                    break;
                }
                long int val = strtol( end, &end, 10 );
//                printf( "val %ld nnames %d\n", val, nnames );
                if ( val < nnames ) {
                    const name_list_t *cur = names;
                    for ( int i = 0; i < val; ++i ) {
                        cur = cur->next;
                    }
                    to_remove[k++] = cur->name;
                }
                while ( *end == ' ' && end - buffer < n ) ++end;
            }
            if ( k > 0 ) {
                bool do_remove = false;
                if ( tc->confirm ) {
                    printf( " Confirm removing files:\n" );
                    for ( int i = 0; i < k; ++i ) {
                        printf( "  %s\n", to_remove[i] );
                    }
                    printf( "> Enter Y or N: ");
                    fflush( stdout );
                    int n = read( fileno(stdin), buffer, 10 );
                    if ( n == 2 && (buffer[0] & 0x5f) == 'Y' ) {
                        do_remove = true;
                    }
                } else {
                    do_remove = true;
                }
                if ( do_remove ) {
                    for ( int i = 0; i < k; ++i ) {
                        if ( -1 == remove( to_remove[i] ) ) {
                            printf( "Failed to remove %s\n", to_remove[i] );
                        }
                    }
                    free(to_remove);
                    free( buffer );
                    return false;
                }
            }
            free(to_remove);
        } else {
            printf( "Leaving both files\n" );
            free( buffer );
            return false;
        }
    }
}

static bool compare_all( target_context_t *tc, size_t size,
                         const name_list_t *name_list )
{
    // if nothing to compare ()0 or 1 name), just return
    if ( NULL == name_list || NULL == name_list->next ) return false;

    // list is modified below - must make a copy of the original map content
    name_list_t *list = duplicate_list( name_list );
    name_list_t *same;

    bool stop = false;
    while ( true ) {
        same = list;        // move head of list in same (tentatively)
        list = list->next;  // remove head from list
        list->prev = NULL;
        same->next = NULL;
        same->prev = NULL;
        name_list_t *last_same = same;

        FILE *f1 = fopen( same->name, "rb" );
        if ( NULL == f1 ) {
            printf( "Failed to open file %s (errno %d) exiting\n",
                    same->name, errno );
            exit(FILE_IO_ERROR);
        }

        name_list_t *next_item;
        for ( name_list_t *item = list; item; item = next_item ) {
            FILE *f2 = fopen( item->name, "rb" );
            if ( NULL == f2 ) {
                printf( "Failed to open file %s (errno %d) exiting\n",
                        item->name, errno );
                exit(FILE_IO_ERROR);
            }
            next_item = item->next;
            if ( bin_compare( f1, f2 ) ) {  // same: move item to same list
                if ( NULL != item->prev ) {
                    item->prev->next = item->next;
                } else {
                    list = item->next;
                }
                if ( NULL != item->next ) {
                    item->next->prev = item->prev;
                }
                last_same->next = item;
                item->next = NULL;
                item->prev = last_same;
                last_same = item;
            }
            fclose( f2 );
        }
        if ( same->next ) { // at least 2 names in same list
            name_list_t *cur = same;
            int nnames = 0;
            printf( "size %ld\n", size );
            while ( cur ) {
                printf( "  %s\n", cur->name );
                ++nnames;
                cur = cur->next;
            }
            tc->redundant += nnames - 1;    // all but one are redundant
            if ( tc->remove ) {    // ask which names to remove (sep with ' ')
                stop = interactive_remove_files( tc, same, nnames );
            }
        }
        fclose( f1 );
        free_duplicate_list( same );
        if ( stop ) {
            free_duplicate_list( list );
            break;
        }
        if ( NULL == list || NULL == list->next ) {
            if ( NULL != list ) {
                free_duplicate_list( list );
            }
            break;      // single left in original list cannot match any other
        }
    }
    return stop;
}

// called for every map entry, unless it returns true
static bool visit_entries( uint32_t index, const void *key,
                           const void *data, void *ctxt )
{
    (void)index;
    target_context_t *tc = ctxt;
    size_t size = (size_t)key;
    const name_list_t *list = data;

    if ( tc->compare ) {        // compare all files with same size
        return compare_all( tc, size, list );
    } else if ( list->next ) {  // list all files with same size if more than 1
        printf( "size %ld\n", size );
        for ( const name_list_t *ntry = list; NULL != ntry; ntry = ntry->next ) {
            printf( "  %s\n", ntry->name );
            ++tc->redundant;
        }
    }
    return false;
}

// compare single file/dir target to all duplicates
static bool compare_target( char *path, size_t size, void *context )
{
    target_context_t *tc = context;
    if ( 0 == size ) {
        if ( tc->zero ) {
            printf( "Empty target file %s\n", path );
        }
        return true;
    }
    const name_list_t *list = map_lookup_entry( tc->map, (void *)size );
    if ( NULL == list ) {   // no file matching the target file size
        return true;
    }

    if ( ! tc->compare ) {
        printf( "size %ld\n  <target> %s\n", size, path );
        for ( const name_list_t *ntry = list; NULL != ntry; ntry = ntry->next ) {
            printf( "  %s\n", ntry->name );
        }
        return false;
    }

    FILE *f1 = fopen( path, "rb" );
    if ( NULL == f1 ) {
        printf( "Error: Failed to open target file %s (errno %d) exiting\n",
                path, errno );
        exit(FILE_IO_ERROR);
    }

    tc->path = path;
    // binary content comparison is required
    int nnames = 0;
    name_list_t *duplicates, *prev;
    duplicates = prev = NULL;
    for ( const name_list_t *ntry = list; NULL != ntry; ntry = ntry->next ) {
        FILE *f2 = fopen( ntry->name, "rb" );
        if ( NULL == f2 ) {
            printf( "Failed to open file %s (errno %d) exiting\n",
                    ntry->name, errno );
            exit(FILE_IO_ERROR);
        }
        if ( bin_compare( f1, f2 ) ) {  // same content
            ++tc->redundant;
            if ( 0 == nnames ) {
                printf( "size %ld\n  <target> %s\n", size, path );
                if ( tc->remove ) {
                    duplicates = malloc_or_exit( sizeof( name_list_t ) );
                    duplicates->name = path;
                    duplicates->next = NULL;
                    duplicates->prev = NULL;
                    prev = duplicates;
                }
            }
            printf( "  %s\n", ntry->name );
            if ( tc->remove ) {
                name_list_t *to_remove = malloc_or_exit( sizeof( name_list_t ) );
                to_remove->name = ntry->name;
                to_remove->next = NULL;
                to_remove->prev = prev;
                prev->next = to_remove;
                prev = to_remove;
            }
            ++ nnames;
        }
    }
    fclose( f1 );
    if ( tc->remove ) {
        interactive_remove_files( tc, duplicates, nnames );
    }
    free_duplicate_list( duplicates );
    return false;
}

extern void process_duplicates( map_t *map, args_t *args )
{
    magic_t magic = open_magic_lib( );
#ifdef TIME_MEASURE
    uint64_t file_process_start = get_nanosecond_timestamp();
#endif
    target_context_t tc;
    tc.cookie = magic;
    tc.map = map;
    tc.redundant = 0;
    tc.compare = args->compare;
    tc.remove = args->remove;
    tc.confirm = args->confirm;

    if ( NULL != args->target ) {   // single target file/dir case
        struct stat stat_data;
        if ( 0 != stat( args->target->path, &stat_data ) ) {
            printf( "Error: unable to stat target file %s\n", args->target->path );
            exit(FILE_IO_ERROR);
        }
        tc.zero = args->target->zero;
        if ( S_ISREG( stat_data.st_mode ) ) {   // Handle single regular file
            compare_target( args->target->path, stat_data.st_size, &tc );
        } else if ( S_ISDIR( stat_data.st_mode ) ){ // Handle single directory
            traverse_directory( args->target->path, args->target->nosub,
                                compare_target, &tc );
        } else {
            printf( "Target %s is a special file: mode 0x%x - exiting\n",
                    args->target->path, stat_data.st_mode );
        }
    } else {
        tc.path = NULL;
        map_process_entries( map, visit_entries, (void *)&tc );
    }
    if ( ! tc.remove ) {
#ifdef TIME_MEASURE
        printf( "Time elapsed processing files %ld milliseconds\n",
                            get_nanosecond_timestamp() - file_process_start );
#endif
        printf( "Found %ld redundant files\n", tc.redundant );
    }
    close_magic_lib( magic );
}

typedef struct {
    map_t           *map;
    size_t          count;
    bool            zero;
} map_context_t;

// called for a single target file, or for each target file in a directory
// Always return true to free the target path if called from traverse_directory
static bool check_target_content( char *path, size_t size, void *context )
{
    map_context_t *mcp = context;
    if ( 0 == size ) {
        if ( mcp->zero ) {
            printf( "Empty target file %s\n", path );
        }
        return true;
    }
    FILE *target = fopen( path, "rb" );
    if ( NULL == target ) {
        printf( "Failed to open target file %s (errno %d) exiting\n",
                path, errno );
        exit(FILE_IO_ERROR);
    }
    const name_list_t *list = map_lookup_entry( mcp->map, (void *)size  );
    bool found = false;
    if ( NULL != list  ) {
        for ( const name_list_t *entry = list; NULL != entry; entry = entry->next ) {
            const char *name = entry->name;
            FILE *f = fopen( name, "rb" );
            bool match = bin_compare( target, f );  // at least one matching file found
            fclose( f );

            if ( match ) {
                printf( " %s content is found as %s\n", path, name );
                found = true;
            }
        }
    }
    if ( ! found ) {
        printf( " %s content is not found in any path\n", path );
    }
    fclose( target );
    return true;
}

extern void search_targets( map_t *map, args_t *args )
{
#ifdef TIME_MEASURE
    uint64_t file_process_start = get_nanosecond_timestamp();
#endif
    if ( NULL != args->target ) {
        map_context_t ctxt;
        ctxt.map = map;
        ctxt.zero = args->target->zero;
        ctxt.count = 0;

        struct stat stat_data;
        if ( 0 != stat( args->target->path, &stat_data ) ) {
            printf( "Error: unable to stat target file %s\n", args->target->path );
            exit(FILE_IO_ERROR);
        }
        if ( S_ISREG( stat_data.st_mode ) ) {       // Handle regular file
//            printf( "Target is a regular file\n" );
            check_target_content( args->target->path, stat_data.st_size, &ctxt );

        } else if ( S_ISDIR( stat_data.st_mode ) ) { // Handle directory
//            printf( "Target is a directory\n" );
            traverse_directory( args->target->path, args->target->nosub,
                                check_target_content, &ctxt );
        } else {
            printf( "Warning: Target is a special file - skipping\n" );
        }
    }
#ifdef TIME_MEASURE
    printf( "Time elapsed processing files %ld milliseconds\n",
                            get_nanosecond_timestamp() - file_process_start );
#endif
}

static bool build_map( char *path, size_t size, void *context )
{
    map_context_t *mcp = context;

    if ( 0 != size ) {
        ++mcp->count;
        name_list_t *head = (void *)map_lookup_entry( mcp->map, (void *)size );
        name_list_t *ntry = malloc_or_exit( sizeof( name_list_t ) );
        ntry->name = path;
        ntry->next = NULL;

        if ( NULL == head ) {
            ntry->prev = ntry;
            map_insert_entry( mcp->map, (void *)size, (void*)ntry );
        } else {
            ntry->prev = head->prev;
            head->prev->next = ntry;
            head->prev = ntry;
        }
        return false;
    } else {
        if ( mcp->zero ) {
            printf( "Empty file %s\n", path );
        }
    }
    return true;
}

extern map_t *collect_same_size_files( args_t *args )
{
    // By default start with a medium size map table.
    // Map entries are defined as key=size, value = (name_list_t *)
    map_t *map = new_map( NULL, NULL,
                          INITIAL_HASH_SIZE, MAX_COLLISIONS );
    if ( NULL == map ) {
        return NULL;
    }

    map_context_t ctxt;
    ctxt.map = map;
    ctxt.count = 0;
#ifdef TIME_MEASURE
    int64_t start = get_nanosecond_timestamp( );
#endif
    for ( search_t *sptr = args->paths; NULL != sptr->path; ++sptr ) {
        ctxt.zero = sptr->zero;
        traverse_directory( sptr->path, sptr->nosub, build_map, &ctxt );
    }
#ifdef TIME_MEASURE
    int64_t stop = get_nanosecond_timestamp( );
    printf( "Time elapsed building map: %ld milliseconds\n", NANOSEC_TO_MILLISEC(stop-start) );
#endif
    printf( "Traversed %ld files\n", ctxt.count );
    return map;
}

static bool free_entry( uint32_t index,
                        const void *key, const void *data, void *ctxt )
{
    (void)index;
    (void)key;
    (void)ctxt;

    name_list_t *entry = (void *)data;
    while ( NULL != entry ) {
        free( entry->name );
        name_list_t *to_remove = entry;
        entry = entry->next;
        free( to_remove );
    }
    return false;
}

extern void free_collected_data( map_t *map )
{
    map_process_entries( map, free_entry, NULL );
    map_free( map );
}
