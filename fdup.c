
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include "comp.h"

static void help( void )
{
    printf( "fdup -h -cnrwzt=<path> [-nz <path>]*\n\n" );
    printf( "look for multiple instances of the same file content in all\n" );
    printf( "directories and their sub-directories\n\n" );
    printf( "Options:\n" );
    printf( "   -h          print this help message and exit.\n" );
    printf( "   -c          compare file contents. By default, check only if file\n");
    printf( "               sizes are the same.\n" );
    printf( "   -n          do not enter subdirectories. This option applies only\n");
    printf( "               to the following path and may be repeated before each\n");
    printf( "               directory path to search\n");
    printf( "   -N          same as -n but it applies to all following paths\n" );
    printf( "   -r          remove some of the same files. By default, just list\n" );
    printf( "               their names. The list of file(s) to remove is requested\n" );
    printf( "   -w          removal with extra confirmation after files are selected\n" );
    printf( "   -z          show empty files while traversing directories. This\n");
    printf( "               option applies only to the following path and may be\n");
    printf( "               repeated before each path to search\n");
    printf( "   -Z          same as -z but it applies to all following paths\n" );
    printf( "   -t=<path>   set single target to find duplicates of\n\n" );

    printf( "The following paths are the pathnames of the root directories to scan\n" );
    printf( "for identical files. If absent, the current directory is used instead.\n\n" );

    printf( "Notes:\n" );
    printf( "   With no option selected, fdup displays the path of all files\n" );
    printf( "   with the same size. To check if file contents are truly\n" );
    printf( "   identical, select the option -c.\n\n" );
    printf( "   Options -r and -w are ignored if option -c is not selected.\n\n" );
    printf( "   Option -r tries to remove some identical files. All identical files\n" );
    printf( "   are listed and the indexes of the files to remove in the list are\n" );
    printf( "   requested. If none are selected, no removal happens. If the index is\n" );
    printf( "   invalid, the question is asked again.\n\n" );
    printf( "   Option -w asks for extra confirmation after the files to removed have\n" );
    printf( "   been selected, before removal happens.\n\n" );
    printf( "   Without option -t, the directories are scanned and each file found\n" );
    printf( "   is compared against all others. The result is a list of files that\n" );
    printf( "   have duplicates under the same or different name somehwere in the\n" );
    printf( "   directory trees. Option -t restricts the comparison to the specific\n" );
    printf( "   file given by option -t.\n\n" );
}

static void get_args( int argc, char **argv, args_t *args )
{
    args->paths = NULL;
    args->target = NULL;
    args->absent = false;
    args->compare = false;
    args->remove = false;
    args->confirm = false;
    bool zero_default = false;
    bool zero = false;
    bool nosub_default = false;
    bool nosub = false;

    int n_paths = 0;
    bool expecting_target = false;

    for ( int i = 1; i < argc; ++i ) {
        char *arg = argv[i];
        switch (*arg) {
        case '-':
            for ( int j = 1; 0 != arg[j]; ++j) {
                switch( arg[j] ) {
                case 'h': case 'H':
                    help();
                    exit(0);
                case 'c':
                    args->compare = true;
                    break;
                case 'n':
                    nosub = true;
                    break;
                case 'N':
                    nosub = nosub_default = true;
                    break;
                case 'r':
                    args->remove = true;
                    break;
                case 't':
                    if ( NULL != args->target ) {
                        error( "multiple targets definitions");
                    }
                    if ( '\0' == arg[j+1] ) {
                        expecting_target = true;
                        break;
                    }
                    if ( '=' == arg[j+1] ) {
                        set_new_target( args, &arg[j+2], nosub, zero );
                        nosub = nosub_default;
                        zero = zero_default;
                        j += 1 + strlen(&arg[j+2]);
                        break;
                    }
                    error( "-t requires '=' or ' ' before path" );
                    break;
                case 'w':
                    args->confirm = true;
                    break;
                case 'z':
                    zero = true;
                    break;
                case 'Z':
                    zero = zero_default = true;
                    break;
                default:
                    printf( "-%c: ", arg[j] );
                    error( "unrecognized option" );
                }
            }
            break;

        default:
            if ( expecting_target ) {
                set_new_target( args, arg, nosub, zero );
                nosub = nosub_default;
                zero = zero_default;
                expecting_target = false;
                break;
            }
            // worse case all following arguments are directory paths
            if ( NULL ==  args->paths ) {
                args->paths = new_paths(1 + argc - i);  // +NULL to end the list
                n_paths = 0;                            // index in paths
            }
            set_path( args, n_paths, argv[i], nosub, zero );
            nosub = nosub_default;
            zero = zero_default;
            ++n_paths;
            break;
        }
    }

    if ( NULL == args->paths ) {
        args->paths = new_paths(2);
        set_path( args, 0, getcwd( NULL, 4096 ), nosub, zero );
        set_path( args, 1, NULL, false, false );
    }

    if ( args->compare == false ) {
        if ( args->remove) {
            printf( "WARNING: option -r is ignored when option -c is not given\n" );
        }
        args->remove = false;
    }
    if ( args->remove == false ) {
        args->confirm = false;
    }
}

int main( int argc, char**argv )
{
    args_t  args;
    get_args( argc, argv, &args );
#if 0
    char *target_path = ( NULL == args.target ) ? NULL : args.target->path;
    printf( "compare %d remove %d, confirm %d target: %s paths:\n",
            args.compare, args.remove, args.confirm, target_path );
    for ( int i = 0; ; ++i ) {
        if ( NULL == args.paths[i].path ) {
            break;
        }
        printf( " name %s nsub=%d, zero=%d\n",
                args.paths[i].path, args.paths[i].nosub, args.paths[i].zero );
    }
#endif
    map_t *map = collect_same_size_files( &args );
    process_same_size_files( map, &args );
    free_collected_data( map );
    free_target_n_paths( &args );
}
