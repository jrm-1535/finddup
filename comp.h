
#ifndef __COMP_H__
#define __COMP_H__

#include <stdbool.h>
#include "map.h"


extern map_t *collect_similar_files( char *root_path );

extern void check_files( map_t *map, bool compare,
                         bool remove, bool interactive, bool confirm );

extern void free_collected_data( map_t *map );

#endif /* __COMP_H__ */
