#include "history.h"

#define CLIPBOARD_MOD_NAME "clipboard"
#define DATA_DIR    CLIPBOARD_MOD_NAME
#define HISTORY_NAME "history"
#define HISTORY_VERSION     1 /* must be < 9  */

#if 0
  #define PATH_MAX 4096
  /** PATH_MAX is defined in <linux/limits.h>
   *     While this define is used through out enlightenment source code
   *     its use is perhaps a bad idea.
   *
   * see http://insanecoding.blogspot.com/2007/11/pathmax-simply-isnt.html
   *    "Since a path can be longer than PATH_MAX, the define is useless,
   *     writing code based off of it is wrong, and the functions that
   *     require it are broken."
   *
   * Regardless in the e-dev tradition we are using it here.
   */
#endif

/* convenience macros to compress code */
#define CALLOC_DIGIT_STR(str, n)                     \
do {                                                 \
  long _tempn_ = n, _digits_ = 1;                    \
  while (_tempn_ /= 10) _digits_++;                  \
  str = malloc(_digits_ + 1);                        \
  if (!str) {                                        \
    /* This is bad, leave it to calling function */  \
    CRI("ERROR: Memory allocation Failed!!");        \
    eet_close(history_file);                         \
    return EET_ERROR_OUT_OF_MEMORY;                  \
   }                                                 \
   sprintf(str, "%d", 0);                            \
 } while(0)

#define PATH_MAX_ERR                                              \
do {                                                              \
  ERR("PATH_MAX exceeded. Need Len %d, PATH_MAX %d", len, PATH_MAX); \
  strcpy(path, "");                                               \
  success = EINA_FALSE;                                           \
 } while(0)                                                       \

/* Private Funcions */
Eina_Bool _mkpath_if_not_exists(const char *path);
Eina_Bool _set_data_path(char *path);
Eina_Bool _set_history_path(char *path);

/**
 * @brief Creates path if non-existant
 *
 * @param path char array to create.
 * @return EINA_TRUE on success EINA_FALSE otherwise
 *
 */
Eina_Bool
_mkpath_if_not_exists(const char *path)
{
    EINA_SAFETY_ON_NULL_RETURN_VAL(path, EINA_FALSE);

    Eina_Bool success = EINA_TRUE;

    if(!ecore_file_exists(path))
       return ecore_file_mkdir(path);
    return success;
}

/**
 * @brief Sets the XDG_DATA_HOME location for saving clipboard history data.
 *
 * @param path char array to store history path in.
 * @return EINA_TRUE on success EINA_FALSE otherwise
 *
 */
Eina_Bool
_set_data_path(char *path)
{
    EINA_SAFETY_ON_NULL_RETURN_VAL(path, EINA_FALSE);

    /* FIXME: Non-portable nix only code */
    const char *temp_str = NULL;
    Eina_Bool success = EINA_TRUE;

    /* See if XDG_DATA_HOME is defined
     *     if so use it
     *     if not use XDG_DATA_HOME default
     */
    temp_str = getenv("XDG_DATA_HOME");
    if (temp_str && temp_str[0] == '/' ) {
      const size_t len = snprintf(NULL, 0, "%s", temp_str) 
                              + 1 + (temp_str[strlen(temp_str)] != '/');
      if (len <= PATH_MAX) {
        snprintf(path, strlen(temp_str)+1, "%s", temp_str);
        // Ensure XDG_DATA_HOME terminates in '/'
        if (path[strlen(path)-1] != '/')
          strcat(path, "/");
      }
      else
        PATH_MAX_ERR;
    }
    /* XDG_DATA_HOME default */
    else {
      if (temp_str && temp_str[0] != '/')
        WRN("Malformed XDG_DATA_HOME path: %s", temp_str);
      struct passwd *pw = NULL;
      pw = getpwuid(getuid());
      temp_str = pw->pw_dir;
      const size_t len = snprintf(NULL, 0, "%s/.local/share/", temp_str) + 1;
      if (len <= PATH_MAX) {
        // Hopefully unnecessary Safety check
        if (temp_str)
           sprintf(path,"%s/.local/share/",temp_str);
        else {
           // Should never happen
           strcpy(path, "");
           success = EINA_FALSE;
        }
     }
     else
        PATH_MAX_ERR;
    }
    return success;
}

/**
 * @brief Sets the path and file name for saving clipboard history data.
 *
 * @param history_path char array to store history path in.
 * @return EINA_TRUE on success EINA_FALSE otherwise
 *
 * This function not only sets the history path but also creates the needed
 * directories if they do not exist.
 *
 */
Eina_Bool
_set_history_path(char *path)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(path, EINA_FALSE);

   Eina_Bool success = EINA_TRUE;

   if(_set_data_path(path)) {
       const size_t len = snprintf(NULL, 0, "%s%s/%s", path, CLIPBOARD_MOD_NAME, HISTORY_NAME) + 1;
       if (len <= PATH_MAX) {
         sprintf(path,"%s%s/", path, CLIPBOARD_MOD_NAME);
         success = _mkpath_if_not_exists(path);
         strcat(path, HISTORY_NAME);
       }
       else
        PATH_MAX_ERR;
   } else
       success = EINA_FALSE;
   return success;
}

/**
 * @brief  Reads clipboard Instantance history  from a binary file with location
 *           specified by FreeDesktop XDG specifications
 *
 * @param  items,        the address of an Eina_List to fill with history file
 *         ignore_ws     flag to ignore whitespace in setting label name
 *         label_length  number of characters to store in label name
 *
 * @return  EET_ERROR_NONE on success
 *          Eet error identifier of error otherwise.
 *          On error *items set to NULL
 */
Eet_Error
read_history(Eina_List **items, unsigned ignore_ws, unsigned label_length)
{
    Eet_File *history_file;
    Clip_Data *cd = NULL;
    Eina_List *l = NULL;
    char *ret, *str, history_path[PATH_MAX];
    unsigned int i, size;
    long version, item_num;

    /* Open history file */
    if(!_set_history_path(history_path)) {
      ERR("History File Creation Error: %s", history_path);
      return EET_ERROR_BAD_OBJECT;
    }
    history_file = eet_open(history_path, EET_FILE_MODE_READ);
    if (!history_file) {
      ERR("Failed to open history file: %s", history_path);
      *items = NULL;
      return EET_ERROR_BAD_OBJECT;
    }
    /* Check History Version */
    ret = eet_read(history_file, "VERSION", &size);
    if (!ret){
      INF("No version number in history file");
      ret = "0";
    }
    version = strtol(ret, NULL, 10);
    if (version && version != HISTORY_VERSION) {
      INF("History file version mismatch, deleting history");
      *items = NULL;
      return eet_close(history_file);
    }
    /* Read Number of items */
    ret = eet_read(history_file, "MAX_ITEMS", &size);
    if (!ret) {
      ERR("History file corruption: %s", history_path);
      *items = NULL;
      return eet_close(history_file);
    }
    /* If we have no items in history wrap it up and return. */
    item_num = strtol(ret, NULL, 10);
    if (item_num <= 0) {
      INF("History file empty or corrupt: %s", history_path);
      *items = NULL;
      return eet_close(history_file);
    }
    /* Malloc properly sized str */
    CALLOC_DIGIT_STR(str, item_num);
    /* Read each item */
    for (i = 1; i <= item_num; i++){
        cd = E_NEW(Clip_Data, 1);
        sprintf(str, "%d", i);
        ret = eet_read(history_file, str, &size);
        if (!ret) {
          ERR("History file corruption: %s", history_path);
          *items = NULL;
          if (l)
            E_FREE_LIST(l, free_clip_data);
          free(str);
          return eet_close(history_file);
        }
        // FIXME: DATA VALIDATION
        cd->content = strdup(ret);
        set_clip_name(&cd->name, cd->content,
                      ignore_ws, label_length);
        l = eina_list_append(l, cd);
    }
    /* and wrap it up */
    free(ret);
    free(str);
    *items = l;
    return eet_close(history_file);
}

/**
 * @brief  Saves clipboard Instantance history  in a binary file with location
 *           specified by FreeDesktop XDG specifications
 *
 * @param  items, the Eina_List to save to the history file
 *
 * @return  EET_ERROR_NONE on success
 *          Eet error identifier of error otherwise.
 *
 * This function not only sets the history path but will also create the needed
 *    directories if they do not exist. See warnings in auxillary functions.
 *
 */
Eet_Error
save_history(Eina_List *items)
{
    Eet_File *history_file;
    Eina_List *l;
    Clip_Data *cd;
    char history_path[PATH_MAX], *str;
    unsigned int n, i = 1;
    Eet_Error ret;

    /* Open history file */
    if(!_set_history_path(history_path)) {
      ERR("History File Creation Error: %s", history_path);
      return EET_ERROR_BAD_OBJECT;
    }
    history_file = eet_open(history_path, EET_FILE_MODE_WRITE);

    if (history_file) {
      /* Malloc properly sized str */
      /*   if !items, 0 items is assumed */
      n = eina_list_count(items);
      CALLOC_DIGIT_STR(str,n);
      /* Write history version */
      sprintf(str, "%d", (HISTORY_VERSION > 9 ? 9 : HISTORY_VERSION));
      eet_write(history_file, "VERSION",  str, strlen(str) + 1, 0);
      /* If we have no items in history wrap it up and return */
      if(!items) {
        sprintf(str, "%d", 0);
        eet_write(history_file, "MAX_ITEMS",  str, strlen(str) + 1, 0);
        free(str);
        return eet_close(history_file);;
      }
      /* Otherwise write each item */
      EINA_LIST_FOREACH(items, l, cd) {
        sprintf(str, "%d", i++);
        eet_write(history_file, str,  cd->content, strlen(cd->content) + 1, 0);
      }
      /* and wrap it up */
      eet_write(history_file, "MAX_ITEMS",  str, strlen(str) + 1, 0);
      ret = eet_close(history_file);
      free(str);
    } else {
      ERR("Unable to open history file: %s", history_path);
      return  EET_ERROR_BAD_OBJECT;
    }
    return ret;
}
