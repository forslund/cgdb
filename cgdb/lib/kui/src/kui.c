/* includes {{{ */

#include <string.h> /* strdup */
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "kui.h"
#include "error.h"
#include "sys_util.h"
#include "io.h"
#include "kui_term.h"

/* }}} */

/* struct kui_map {{{ */

/**
 * A kui map structure.
 *
 * This is simply a key/value pair as far as the outside
 * world is concerned. 
 */
struct kui_map {

	/**
	 * The key is the key strokes the user must type for this map
	 * to work. However, there can be sequences of char's encoded in this 
	 * string that represent another value. For example <ESC> would 
	 * represent CGDB_KEY_ESC.
	 */
	char *original_key;

	/**
	 * This is the literal list of keys the user must type for this map to
	 * be activated. It is a list of int's because it can contain things like
	 * CGDB_KEY_ESC, ..
	 */
	int *literal_key;

	/**
	 * The value is the substitution data, if the key is typed.
	 */
	char *original_value;

	/**
	 *
	 */
	int *literal_value;
};

struct kui_map *kui_map_create (
		const char *key_data, 
		const char *value_data ) {

	struct kui_map *map;
	char *key, *value;
	

	/* Validify parameters */
	if ( !key_data || !value_data )
		return NULL;

	map = (struct kui_map *)malloc ( sizeof ( struct kui_map ) );
	if ( !map )
		return NULL;

	key = strdup ( key_data );

	if ( !key ) {
		kui_map_destroy ( map );
		return NULL;
	}
	
	value = strdup ( value_data );

	if ( !value ) {
		kui_map_destroy ( map );
		return NULL;
	}

	map->original_key = key;
	map->original_value = value;

	if ( kui_term_string_to_cgdb_key_array ( map->original_key, &map->literal_key ) == -1 ) {
		kui_map_destroy ( map );
		return NULL;
	}

	if ( kui_term_string_to_cgdb_key_array ( map->original_value, &map->literal_value ) == -1 ){
		kui_map_destroy ( map );
		return NULL;
	}
	
	return map;
}

int kui_map_destroy ( struct kui_map *map ) {

	if ( !map)
		return -1;

	if ( map->original_key ) {
		free ( map->original_key );
		map->original_key = NULL;
	}

	if ( map->original_value ) {
		free ( map->original_value );
		map->original_value = NULL;
	}

	if ( map->literal_key ) {
		free ( map->literal_key );
		map->literal_key = NULL;
	}

	if ( map->literal_value ) {
		free ( map->literal_value );
		map->literal_value = NULL;
	}

	free (map);
	map = NULL;

	return 0;
}

static int kui_map_destroy_callback ( void *param ) {
	struct kui_map *map = (struct kui_map *)param;
	return kui_map_destroy ( map );
}

int kui_map_get_key ( struct kui_map *map, char **key ) { 

	if ( !map )
		return -1;

	*key = map->original_key;

	return 0;
}

int kui_map_get_literal_key ( struct kui_map *map, int **key ) { 

	if ( !map )
		return -1;

	*key = map->literal_key;

	return 0;
}

int kui_map_get_value ( struct kui_map *map, char **value ) {

	if ( !map )
		return -1;

	*value = map->original_value;

	return 0;
}

int kui_map_get_literal_value ( struct kui_map *map, int **value ) {

	if ( !map )
		return -1;

	*value = map->literal_value;

	return 0;
}

int kui_map_print_cgdb_key_array ( struct kui_map *map ) {
	if ( !map )
		return -1;

	if ( kui_term_print_cgdb_key_array ( map->literal_value ) == -1 )
		return -1;

	return 0;

}

/* }}} */

/* struct kui_map_set {{{ */

/* Kui map set */

enum kui_map_state {
	KUI_MAP_FOUND,
	KUI_MAP_STILL_LOOKING,
	KUI_MAP_NOT_FOUND,
	KUI_MAP_ERROR
};

/** 
 * This maintains a list of maps.
 * Basically, a key/value pair list.
 */
struct kui_map_set {
	/**
	 * The list of maps available.
	 */
	std_list map_list;

	/**
	 * The iterator, pointing to the current list item matched.
	 */
	std_list_iterator map_iter;

	/**
	 * The state of this current map_set.
	 */
	enum kui_map_state map_state;

	/**
	 * If a map was found at any point, this flag get's set to 1.
	 * Otherwise it is 0.
	 * This helps solve the case where you have
	 * map a d
	 * map abc d
	 *
	 * Now, if 'a' is typed, the map_state is set to KUI_MAP_FOUND, however,
	 * if the user keeps typeing, say maybe 'b', then the map_state will be
	 * KUI_MAP_STILL_LOOKING because it is trying to complete the longer 
	 * mapping. If this is the case, is_found should be 1, to tell the outside
	 * world the a mapping was found.
	 */
	int is_found;

	/**
	 * An iterator pointing to the found mapping. Read above.
	 */
	std_list_iterator map_iter_found;
};

struct kui_map_set *kui_ms_create ( void ) {

	struct kui_map_set *map;

	map = (struct kui_map_set *)malloc(sizeof(struct kui_map_set));

	if ( !map )
		return NULL;

	map->map_list = std_list_create ( kui_map_destroy_callback );

	if ( !map->map_list ) {
		kui_ms_destroy ( map );
		return NULL;
	}

	return map;
}

int kui_ms_destroy ( struct kui_map_set *kui_ms ) {
	if ( !kui_ms )
		return -1;

	if ( kui_ms->map_list ) {
		if ( std_list_destroy ( kui_ms->map_list ) == -1 )
			return -1;

		kui_ms->map_list = NULL;
	}

	free (kui_ms);
	kui_ms = NULL;

	return 0;
}

/**
 * A compare function for 2 int arrays.
 * The int arrays should be null terminated.
 *
 * \param one
 * The first item to compare
 *
 * \param two
 * The second item to compare
 *
 * @return
 * returns an integer less than, equal to, or greater than zero if the first n bytes of
 * s1 is found, respectively, to be less than, to match, or be greater than the first n
 * bytes of s2.
 *
 */
static int intcmp ( const int *one, const int *two ) {
	int i = 0;
	int retval = 0;

	while ( 1 ) {
		/* They both are at the end */
		if ( one[i] == 0 && two[i] == 0 ) {
			retval = 0;
			break;
		} else if ( one[i] == 0 ) {
			retval = -1;
			break;
		} else if ( two[i] == 0 ) {
			retval = 1;
			break;
		} else if ( one[i] < two[i] ) {
			retval = -1;
			break;
		} else if ( one[i] > two[i] ) {
			retval = 1;
			break;
		}

		++i;
	}

	return retval;
}

/* a safe intncmp, it won't over run the bounds */
static int intncmp ( const int *one, const int *two, const int n ) {
	int i = 0;
	int retval = 0;
	int pos = 0;

	while ( pos < n) {
		/* They both are at the end */
		if ( one[i] == 0 && two[i] == 0 ) {
			retval = 0;
			break;
		} else if ( one[i] == 0 ) {
			retval = -1;
			break;
		} else if ( two[i] == 0 ) {
			retval = 1;
			break;
		} else if ( one[i] < two[i] ) {
			retval = -1;
			break;
		} else if ( one[i] > two[i] ) {
			retval = 1;
			break;
		}

		++i;
		++pos;
	}

	return retval;
}

static int intlen ( const int *val ) {
	int length = 0;
	while ( val[length] != 0 )
		++length;

	return length;
}

static int kui_map_compare_callback ( 
		const void *a,
		const void *b ) {
	struct kui_map *one = (struct kui_map *)a;
	struct kui_map *two = (struct kui_map *)b;

	return intcmp ( one->literal_key, two->literal_key );
}

int kui_ms_register_map ( 
		struct kui_map_set *kui_ms,
		const char *key_data, 
		const char *value_data ) {
	struct kui_map *map;
	std_list_iterator iter;

	if ( !kui_ms )
		return -1;

	map = kui_map_create ( key_data, value_data );

	if ( !map )
		return -1;

	/* Find the old map */
	if ( !kui_ms->map_list )
		return -1;

	iter = std_list_find ( kui_ms->map_list, map, kui_map_compare_callback );

	if ( !iter )
		return -1;

	/* the key was found, remove it */
	if ( iter != std_list_end ( kui_ms->map_list ) ) {
		iter = std_list_remove ( kui_ms->map_list, iter );

		if ( !iter )
			return -1;
	}

	if ( std_list_insert_sorted ( kui_ms->map_list, map, kui_map_compare_callback ) == -1 )
		return -1;

	return 0;
}

int kui_ms_deregister_map (
		struct kui_map_set *kui_ms,
		const char *key ) {
	std_list_iterator iter;

	if ( !kui_ms )
		return -1;

	if ( !kui_ms->map_list )
		return -1;

	iter = std_list_find ( kui_ms->map_list, key, kui_map_compare_callback );

	if ( !iter )
		return -1;

	/* The key could not be found */
	if ( iter == std_list_begin ( kui_ms->map_list ) )
		return -2;

	iter = std_list_remove ( kui_ms->map_list, iter );

	if ( !iter )
		return -1;

	return 0;
}

std_list kui_ms_get_maps ( struct kui_map_set *kui_ms ) {

	if ( !kui_ms )
		return NULL;

	return kui_ms->map_list;
}

/**
 * Reset the list of maps to it's original state.
 * This is as if no character was passed to kui_ms_update_state.
 *
 * \param kui_ms
 * The map set to reset
 *
 * @return
 * 0 on success, or -1 on error.
 *
 */
static int kui_ms_reset_state ( struct kui_map_set *kui_ms ) {
	if ( !kui_ms )
		return -1;

	kui_ms->map_iter = std_list_begin ( kui_ms->map_list );

	if ( !kui_ms )
		return -1;

	kui_ms->map_state = KUI_MAP_STILL_LOOKING;

	kui_ms->is_found = 0;

	kui_ms->map_iter_found = NULL;

	return 0;
}

/**
 * Get's the state of the current map set.
 *
 * \param kui_ms
 * The map set to get the state of.
 *
 * \param map_state
 * The map set's map state.
 *
 * @return
 * 0 on success, or -1 on error.
 */
static int kui_ms_get_state ( 
		struct kui_map_set *kui_ms, 
		enum kui_map_state *map_state ) {
	if ( !kui_ms )
		return -1;

	*map_state = kui_ms->map_state;

	return 0;
}

/**
 * This should be called when you are no longer going to call
 * kui_ms_update_state on a kui_ms. It allows the kui map set to finalize some
 * of it's internal state data.
 *
 * \param kui_ms
 * The kui map set to finalize
 *
 * @return
 * 0 on success, or -1 on error.
 */
static int kui_ms_finalize_state ( struct kui_map_set *kui_ms ) {
	if ( !kui_ms )
		return -1;
	
	if ( kui_ms->is_found ) {
		kui_ms->map_state = KUI_MAP_FOUND;
		kui_ms->map_iter = kui_ms->map_iter_found;
	}

	return 0;
}

/**
 * This updates the state of a map set.
 *
 * Basically, the map set receives a character. It also receives the position
 * of that character in the mapping. The map set is then responsible for 
 * checking to see if any of the maps match the particular mapping.
 *
 * \param kui_ms
 * The map set to update
 *
 * \param key
 * The input value to match
 *
 * \param position
 * The position of character in the mapping
 *
 * @return
 * 0 on success, or -1 on error.
 */
static int kui_ms_update_state ( 
		struct kui_map_set *kui_ms, 
		int key,
	    int position ) {
	int *int_matched, *int_cur;
	int strncmp_return_value;
	int cur_length;
	std_list_iterator iter;
	
	/* Verify parameters */
	if ( !kui_ms )
		return -1;

	if ( position < 0 )
		return -1;

	if ( key < 0 )
		return -1;

	/* Assertion: Should only try to update the state if still looking */
	if ( kui_ms->map_state != KUI_MAP_STILL_LOOKING )
		return -1;

	/* Get the original value */
	{
		struct kui_map *map;
		void *data;

		if ( std_list_get_data ( kui_ms->map_iter, &data ) == -1 )
			return -1;

		map = (struct kui_map *)data;

		if ( kui_map_get_literal_key ( map, &int_matched ) == -1 )
			return -1;
	}

	/* Start the searching */
	for ( ; 
	 	  kui_ms->map_iter != std_list_end ( kui_ms->map_list );
	   	  kui_ms->map_iter = std_list_next ( kui_ms->map_iter ) ) {
		struct kui_map *map;
		void *data;

		if ( std_list_get_data ( kui_ms->map_iter, &data ) == -1 )
			return -1;

		map = (struct kui_map *)data;

		if ( kui_map_get_literal_key ( map, &int_cur ) == -1 )
			return -1;

		/* Use intcmp */
		strncmp_return_value = intncmp ( int_matched, int_cur, position );

		/* Once the value is passed, stop looking. */
		if ( ( strncmp_return_value != 0 ) ||
			 ( strncmp_return_value == 0 && int_cur[position] > key ) ) {
			kui_ms->map_state = KUI_MAP_NOT_FOUND;
			break;
		}
		
		/* A successful find */
		if ( strncmp_return_value == 0 && int_cur[position] == key ) {
			kui_ms->map_state = KUI_MAP_STILL_LOOKING;
			break;
		}
	}

	/* It was discovered that this map is not found during the loop */
	if ( kui_ms->map_state == KUI_MAP_NOT_FOUND )
		return 0;

	/* Every item has been checked, the map is not in the list */
	if ( kui_ms->map_iter == std_list_end ( kui_ms->map_list ) ) {
		kui_ms->map_state = KUI_MAP_NOT_FOUND;
		return 0;
	}

	/* At this point, the iter points to the valid spot in the list. 
	 * Decide if the correct state is STILL_LOOKING or FOUND.
	 *
	 * The only way to now if you are at FOUND is to do 2 things.
	 * 1. make sure that the lenght of the position is the length
	 * of the current map value. If they are not the same length,
	 * you are STILL_LOOKING
	 * 2. Make sure the next item in the list, doesn't begin with
	 * the current value. If you are at the last spot in the list,
	 * and the first rule holds, it is definatly FOUND.
	 */
	cur_length = intlen ( int_cur );

	if ( cur_length != position + 1) {
	   return 0; /* STILL_LOOKING */
	} else {
		kui_ms->is_found = 1;
		kui_ms->map_iter_found = kui_ms->map_iter;
	}

	/* If still here, rule 1 passed, check rule 2 */
	iter = std_list_next ( kui_ms->map_iter );

	if ( !iter )
		return -1;

	/* Special case, return FOUND */
	if ( iter == std_list_end ( kui_ms->map_list ) ) {
		kui_ms->map_state = KUI_MAP_FOUND;
		return 0;
	}

	/* Get value and see if it begins with same value */
	{
		struct kui_map *map;
		void *data;

		if ( std_list_get_data ( iter, &data ) == -1 )
			return -1;

		map = (struct kui_map *)data;

		if ( kui_map_get_literal_key ( map, &int_matched ) == -1 )
			return -1;

	}

	/* The value is not the same, found. */
	if ( intncmp ( int_matched, int_cur, position+1 ) != 0 ) {
		kui_ms->map_state = KUI_MAP_FOUND;
		return 0;
	}

	return 0;
}

/* }}} */

/* struct kuictx {{{ */

/**
 * A Key User Interface context.
 */
struct kuictx {
	/**
	 * The list of kui_map_set structures.
	 *
	 * The kui context will use this list when looking for maps.
	 */
	std_list kui_map_set_list;

	/**
	 * A list of characters, used as a buffer for stdin.
	 */
	std_list buffer;

	/**
	 * The callback function used to get data read in.
	 */
	kui_getkey_callback callback;
	
	/**
	 * Milliseconds to block on a read.
	 */
	int ms;

	/**
	 * state data
	 */
	void *state_data;

	/**
	 * The file descriptor to read from.
	 */
	int fd;
};

static int kui_ms_destroy_callback ( void *param ) {
	struct kui_map_set *kui_ms = (struct kui_map_set *)param;

	if ( !kui_ms )
		return -1;

	if ( kui_ms_destroy ( kui_ms ) == -1 )
		return -1;

	kui_ms = NULL;

	return 0;
}

static int kui_ms_destroy_int_callback ( void *param ) {
	int *i = (int*)param;

	if ( !i )
		return -1;

	free ( i );
	i = NULL;

	return 0;
}

struct kuictx *kui_create(
		int stdinfd, 
		kui_getkey_callback callback,
		int ms,
	    void *state_data	) {
	struct kuictx *kctx; 
	
	kctx = (struct kuictx *)malloc(sizeof(struct kuictx));

	if ( !kctx )
		return NULL;

	kctx->callback = callback;
	kctx->state_data = state_data;
	kctx->kui_map_set_list = std_list_create ( kui_ms_destroy_callback );
	kctx->ms = ms;

	if ( !kctx->kui_map_set_list ) {
		kui_destroy ( kctx );
		return NULL;
	}

	kctx->fd = stdinfd;

	kctx->buffer = std_list_create ( kui_ms_destroy_int_callback );

	if ( !kctx->buffer ) {
		kui_destroy ( kctx );
		return NULL;
	}

	return kctx;
}

int kui_destroy ( struct kuictx *kctx ) {
	int ret = 0;

	if ( !kctx )
		return -1;

	if ( kctx->kui_map_set_list ) {
		if ( std_list_destroy ( kctx->kui_map_set_list ) == -1 )
			ret = -1;
		kctx->kui_map_set_list = NULL;
	}

	if ( kctx->buffer ) {
		if ( std_list_destroy ( kctx->buffer ) == -1 )
			ret = -1;
		kctx->buffer = NULL;
	}

	free (kctx);
	kctx = NULL;

	return ret;
}

std_list kui_get_map_sets ( struct kuictx *kctx ) {
	if ( !kctx )
		return NULL;

	return kctx->kui_map_set_list;
}

int kui_add_map_set ( 
		struct kuictx *kctx, 
		struct kui_map_set *kui_ms ) {
	if ( !kctx )
		return -1;

	if ( !kui_ms )
		return -1;

	if ( std_list_append ( kctx->kui_map_set_list, kui_ms ) == -1 )
		return -1;

	return 0;
}

/**
 * This basically get's a char from the internal buffer within the kui context
 * or it get's a charachter from the standard input file descriptor.
 *
 * It only blocks for a limited amount of time, waiting for user input.
 *
 * \param kctx
 * The kui context to operate on.
 *
 * @return
 * 0 on success, or -1 on error.
 */
static int kui_findchar ( struct kuictx *kctx ) {
	int key;
	int length;
	void *data;
	std_list_iterator iter;

	/* Use the buffer first. */
	length = std_list_length ( kctx->buffer );

	if ( length == -1 )
		return -1;

	if ( length > 0 ) {
		/* Take the first char in the list */
		iter = std_list_begin ( kctx->buffer );

		if ( !iter )
			return -1; 
		
		if ( std_list_get_data ( iter, &data ) == -1 )
			return -1;

		/* Get the char */
		key = *(int*)data;

		/* Delete the item */
		if ( std_list_remove ( kctx->buffer, iter ) == NULL )
			return -1;
		
	} else {
		/* otherwise, look to read in a char */
		key = kctx->callback ( kctx->fd, kctx->ms, kctx->state_data );
	}

	return key;
}

/**
 * Updates the state data for each map set in the kui
 *
 * \param kctx
 * The kui context to operate on.
 *
 * @return
 * 0 on success, or -1 on error.
 */
static int kui_reset_state_data ( struct kuictx *kctx ) {
    std_list_iterator iter;
    struct kui_map_set *map_set;
    void *data;

	for ( iter = std_list_begin ( kctx->kui_map_set_list );
		  iter != std_list_end ( kctx->kui_map_set_list );
		  iter = std_list_next ( iter ) ) {

		if ( std_list_get_data ( iter, &data ) == -1 )
			return -1;

		map_set = (struct kui_map_set *)data;

		if ( kui_ms_reset_state ( map_set ) == -1 )
			return -1;
	}

	return 0;
}

/**
 * Updates each list in the kui context.
 *
 * \param kctx
 * The kui context to operate on.
 *
 * \param key
 * The input value to match
 *
 * \param position
 * The position of character in the mapping
 *
 * @return
 * 0 on success, or -1 on error.
 */
static int kui_update_each_list ( 
		struct kuictx *kctx, 
		int key,
	    int position	) {
    std_list_iterator iter;
	struct kui_map_set *map_set;
	void *data;
	enum kui_map_state map_state;

	/* For each active map list (doesn't return KUI_MAP_NOT_FOUND), 
	 * give the char c to let the list update it's internal state. 
	 */
	for ( iter = std_list_begin ( kctx->kui_map_set_list );
		  iter != std_list_end ( kctx->kui_map_set_list );
		  iter = std_list_next ( iter ) ) {

		if ( std_list_get_data ( iter, &data ) == -1 )
			return -1;

		map_set = (struct kui_map_set *)data;

		if ( kui_ms_get_state ( map_set, &map_state ) == -1 )
			return -1;

		if ( map_state != KUI_MAP_NOT_FOUND ) {
			if ( kui_ms_update_state ( map_set, key, position ) == -1 )
				return -1;
		}
	}

	return 0;
}

/**
 * Checks to see if any of the map set's are matching a map.
 * If they are, then the kui context should keep trying to match. Otherwise
 * it should stop trying to match.
 *
 * \param kctx
 * The kui context to operate on.
 *
 * \param should_continue
 * outbound as 1 if the kui context should keep looking, otherwise 0.
 *
 * @return
 * 0 on success, or -1 on error.
 */
static int kui_should_continue_looking (
		struct kuictx *kctx,
		int *should_continue ) {
	std_list_iterator iter;
	struct kui_map_set *map_set;
	void *data;
	enum kui_map_state map_state;

	if ( !kctx )
		return -1;

	if ( !should_continue )
		return -1;

	*should_continue = 0;

	/* Continue if at least 1 of the lists still says 
	 * KUI_MAP_STILL_LOOKING. If none of the lists is at this state, then
	 * there is no need to keep looking
	 */
	for ( iter = std_list_begin ( kctx->kui_map_set_list );
		  iter != std_list_end ( kctx->kui_map_set_list );
		  iter = std_list_next ( iter ) ) {

		if ( std_list_get_data ( iter, &data ) == -1 )
			return -1;

		map_set = (struct kui_map_set *)data;

		if ( kui_ms_get_state ( map_set, &map_state ) == -1 )
			return -1;

		if ( map_state == KUI_MAP_STILL_LOOKING )
			*should_continue = 1;
	}

	return 0;
}

/**
 * Update each map list's state.
 *
 * \param kctx
 * The kui context to operate on.
 *
 * @return
 * 0 on success, or -1 on error.
 */
static int kui_update_list_state ( struct kuictx *kctx ) {
	std_list_iterator iter;
	struct kui_map_set *map_set;
	void *data;

	if ( !kctx )
		return -1;

	/* If a macro was found, change the state of the map_list */
	for ( iter = std_list_begin ( kctx->kui_map_set_list );
		  iter != std_list_end ( kctx->kui_map_set_list );
		  iter = std_list_next ( iter ) ) {

		if ( std_list_get_data ( iter, &data ) == -1 )
			return -1;

		map_set = (struct kui_map_set *)data;

		if ( kui_ms_finalize_state ( map_set ) == -1 )
			return -1;
	}

	return 0;
}

/**
 * Checks to see if a kui map has been found.
 *
 * \param kctx
 * The kui context to operate on.
 *
 * \param was_map_found
 * outbound as 1 if a map was found , otherwise 0.
 *
 * \param the_map_found
 * If was_map_found returns as 1, then this is the map that was found.
 *
 * @return
 * 0 on success, or -1 on error.
 */
static int kui_was_map_found ( 
		struct kuictx *kctx,
		int *was_map_found,
		struct kui_map **the_map_found ) {
	std_list_iterator iter;
	struct kui_map_set *map_set;
	void *data;
	enum kui_map_state map_state;

	if ( !was_map_found )
		return -1;

	if ( !the_map_found )
		return -1;

	*was_map_found = 0;

	/* At this point, the loop exited for one of several reasons.
	 *
	 * Each list has a correct state value. If one of the lists has the value
	 * KUI_MAP_FOUND, then a map was found. This should be the value used.
	 *
	 * If none of the lists has the value kui_map_found, then no map was found.
	 * What a shame. Why did I write all of this code ?!?
	 */
	for ( iter = std_list_begin ( kctx->kui_map_set_list );
		  iter != std_list_end ( kctx->kui_map_set_list );
		  iter = std_list_next ( iter ) ) {

		if ( std_list_get_data ( iter, &data ) == -1 )
			return -1;

		map_set = (struct kui_map_set *)data;

		if ( kui_ms_get_state ( map_set, &map_state ) == -1 )
			return -1;

		if ( map_state == KUI_MAP_FOUND ) {
			void *data;

			if ( std_list_get_data ( map_set->map_iter, &data ) == -1 )
				return -1;

			*the_map_found = ( struct kui_map *)data;
			*was_map_found = 1;
		}
	}


	return 0;
}

/**
 * Updates the kui context buffer.
 *
 * \param kctx
 * The kui context to operate on.
 *
 * \param the_map_found
 * The map that was found, valid if map_was_found is 1
 *
 * \param map_was_found
 * 1 if a map was found, otherwise 0
 *
 * \param position
 * ???
 *
 * \param bufmax
 * ???
 *
 * \param key
 * ???
 *
 * @return
 * 0 on success, or -1 on error.
 *
 * Here is a simple example to help illustrate this algorithm.
 *
 * map ab xyz
 * map abcdf do_not_reach
 *
 * the buffer contained, abcdefgh
 *
 * abcde is read in coming into this function.
 *
 * so, ab matches a map, cde is read in to continue matching
 * but never does. that means fgh is left in the buffer.
 *
 * ab changes to xyz.
 * cde needs to be put back.
 *
 * so you end up with 
 * 	xyzcdefgh
 *
 * which means that you put back the extra char's first.
 * Then you put back the map.
 *
 * The only 2 things to know are, what are the extra chars
 * and what is the value of the map.
 */
static int kui_update_buffer ( 
		struct kuictx *kctx,
		struct kui_map *the_map_found,
	    int map_was_found,
	    int position,
		int *bufmax,
		int *key) {
	int i;
	int j;


	/* Find extra chars */
	if ( map_was_found )
		/* For the example, this would be 'ab' 2 */
		i = intlen ( the_map_found->literal_key );
	else {
		i = 1; /* bufmax[0] will be returned to the user */
		*key = bufmax[0];
	}

	/* for the example, position would be 5 
	 * Assertion: bufmax[position] is valid 
	 */
	for ( j = position ; j >= i; --j ) {
		int *val = malloc ( sizeof ( int ) );
		if ( !val )
			return -1;

		*val = bufmax[j];

		if ( std_list_prepend ( kctx->buffer, val ) == -1 )
			return -1;
	}

	/* Add the map back */
	if ( map_was_found ) {
		int length;

		/* Add the value onto the buffer */
		length = intlen ( the_map_found->literal_value );
		for ( i = length-1; i >= 0; --i ) {
			int *val = malloc ( sizeof ( int ) );
			if ( !val )
				return -1;

			*val = the_map_found->literal_value[i];

			if ( std_list_prepend ( kctx->buffer, val ) == -1 )
				return -1;
		}
	} 

	return 0;
}

/**
 * Get's the next char.
 *
 * \param map_found
 * returns as 0 if no map was found. In this case, the return value is valid.
 * returns as 1 if a mapping was found. In this case the return value is not 
 * valid.
 *
 * @return
 * -1 on error
 * The key on success ( valid if map_found == 0 )
 */
static int kui_findkey ( 
		struct kuictx *kctx,
	    int *was_map_found ) {

	int key;
	int position;
	int should_continue;
	int bufmax[1024]; /* This constant limits this function */
	struct kui_map *the_map_found;

	/* Validate parameters */
	if ( !kctx )
		return -1;

	if ( !was_map_found )
		return -1;

	if ( !kctx->kui_map_set_list )
		return -1;

	/* Initialize variables on stack */
	key = -1;
	position = -1;
	*was_map_found = 0;
	should_continue = 0;

	/* Reset the state data for all of the lists */
	if ( kui_reset_state_data ( kctx ) == -1 )
		return -1;

	/* Start the main loop */
	while ( 1 ) {
		key = kui_findchar ( kctx );

		/* If there is no more data ready, stop. */
		if ( key == 0 )
			break; 

		++position;

		bufmax[position] = key;

		/* Update each list, with the character read, and the position. */
		if ( kui_update_each_list ( kctx, key, position ) == -1 )
			return -1;

		/* Check to see if at least a single map is being matched */
		if ( kui_should_continue_looking ( kctx, &should_continue ) == -1 )
			return -1;

		if ( !should_continue )
			break;
	}

	key = 0; /* This should no longer be used. Enforcing that. */

	/* All done looking for chars, let lists that matched a mapping
	 * be known. ex KUI_MAP_STILL_LOOKING => KUI_MAP_FOUND. This 
	 * happens when 
	 *    map abc   xyz
	 *    map abcde xyz
	 *
     * If the user types abcd, the list will still be looking,
     * even though it already found a mapping.
	 */
	if ( kui_update_list_state ( kctx ) == -1 )
		return -1;

	/* Check to see if a map was found.
	 * If it was, get the map also.
	 */
	if ( kui_was_map_found ( kctx, was_map_found, &the_map_found ) == -1 )
		return -1;

	/* Update the buffer and get the final char. */
	if ( kui_update_buffer ( kctx, the_map_found, *was_map_found, position, bufmax, &key ) == -1 )
		return -1;

	return key;
}

int kui_getkey ( struct kuictx *kctx ) {
	int map_found; 
	int key;

	/* If a map was found, restart the algorithm. */
	do {
		key = kui_findkey ( kctx, &map_found );

		if ( key == -1 )
			return -1;

	} while ( map_found == 1 );

	return key;
}

int kui_cangetkey ( struct kuictx *kctx ) {
	int length;

	/* Use the buffer first. */
	length = std_list_length ( kctx->buffer );

	if ( length == -1 )
		return -1;

	if ( length > 0 )
		return 1;

	return 0;
}

/* }}} */

/* struct kui_manager {{{ */

struct kui_manager {
	struct kuictx *terminal_keys;
	struct kuictx *normal_keys;
};

static int create_terminal_mappings ( struct kuictx *i ) {
	struct kui_map_set *terminal_map;
	
	/* Create the terminal kui map */
	terminal_map = kui_term_get_terminal_mappings ();

	if ( !terminal_map )
		return -1;
	
	if ( kui_add_map_set ( i, terminal_map ) == -1 )
		return -1;

	return 0;
}

int char_callback ( 
		const int fd, 
		const unsigned int ms,
		const void *obj ) {

	return io_getchar ( fd, ms );
}

int kui_callback (
		const int fd, 
		const unsigned int ms,
		const void *obj ) {

	struct kuictx *kctx = (struct kuictx *)obj;
	int result;

	result = kui_cangetkey ( kctx );

	if ( result == -1 )
		return -1;

	if ( result == 1 )
		return kui_getkey ( kctx );

	if ( result == 0 ) {
		if ( io_data_ready ( kctx->fd, ms ) == 1 )
			return kui_getkey ( kctx );	
	}

	return 0;
}

struct kui_manager *kui_manager_create(int stdinfd ) {
	struct kui_manager *man;

	man = ( struct kui_manager* )malloc ( sizeof ( struct kui_manager ) );

	if ( !man )
		return NULL;

	man->terminal_keys = kui_create ( stdinfd, char_callback, 40, NULL );

	if ( !man->terminal_keys ) {
		kui_manager_destroy ( man );
		return NULL;
	}

	if ( create_terminal_mappings ( man->terminal_keys ) == -1 ) {
		kui_manager_destroy ( man );
		return NULL;
	}


	man->normal_keys = kui_create ( -1, kui_callback, 1000, man->terminal_keys );

	if ( !man->normal_keys ) {
		kui_manager_destroy ( man );
		return NULL;
	}

	return man;
}

int kui_manager_destroy ( struct kui_manager *kuim ) {
	int ret = 0;

	if ( !kuim )
		return 0;

	if ( kui_destroy ( kuim->terminal_keys ) == -1 )
		ret = -1;

	if ( kui_destroy ( kuim->normal_keys ) == -1 )
		ret = -1;

	free ( kuim );
	kuim = NULL;

	return ret;
}


std_list kui_manager_get_map_sets ( struct kui_manager *kuim ) {
	if ( !kuim )
		return NULL;

	return kui_get_map_sets ( kuim->normal_keys );
}

int kui_manager_add_map_set ( 
		struct kui_manager *kuim, 
		struct kui_map_set *kui_ms ) {

	if ( !kuim )
		return -1;

	return kui_add_map_set ( kuim->normal_keys, kui_ms );
}

int kui_manager_cangetkey ( struct kui_manager *kuim ) {

	if ( !kuim )
		return -1;

	return kui_cangetkey ( kuim->normal_keys );
}

int kui_manager_getkey ( struct kui_manager *kuim ) {
	if ( !kuim )
		return -1;

	return kui_getkey ( kuim->normal_keys );

}

/* }}} */
