#ifndef ESP_REMOTEFS_H__
#define ESP_REMOTEFS_H__

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <utime.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include <sys/types.h>
#include <sys/reent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <dirent.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_vfs.h"
#include "esp_partition.h"

#ifdef __cplusplus
extern "C" {
#endif


extern void extSend(const char *fmt, ...);

/**
 * @brief a file descriptor
 * That's also a singly linked list used for keeping tracks of all opened file descriptor 
 *
 * Shortcomings/potential issues of 32-bit hash (when CONFIG_LITTLEFS_USE_ONLY_HASH) listed here:
 *     * unlink - If a different file is open that generates a hash collision, it will report an
 *                error that it cannot unlink an open file.
 *     * rename - If a different file is open that generates a hash collision with
 *                src or dst, it will report an error that it cannot rename an open file.
 * Potential consequences:
 *    1. A file cannot be deleted while a collision-geneating file is open.
 *       Worst-case, if the other file is always open during the lifecycle
 *       of your app, it's collision file cannot be deleted, which in the 
 *       worst-case could cause storage-capacity issues.
 *    2. Same as (1), but for renames
 */
typedef struct _vfs_remotefs_file_t {
    //lfs_file_t file;
    int        file;
    uint32_t   hash;
    struct _vfs_littlefs_file_t * next;       /*!< Pointer to next file in Singly Linked List */
#ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
    char     * path;
#endif
} vfs_remotefs_file_t;

/**
 * @brief littlefs definition structure
 */
typedef struct {
    //lfs_t *fs;                                /*!< Handle to the underlying littlefs */
    SemaphoreHandle_t lock;                   /*!< FS lock */
    const esp_partition_t* partition;         /*!< The partition on which littlefs is located */
    char base_path[ESP_VFS_PATH_MAX+1];       /*!< Mount point */

    //struct lfs_config cfg;                    /*!< littlefs Mount configuration */

    vfs_remotefs_file_t *file;                /*!< Singly Linked List of files */

    vfs_remotefs_file_t **cache;              /*!< A cache of pointers to the opened files */
    uint16_t             cache_size;          /*!< The cache allocated size (in pointers) */
    uint16_t             fd_count;            /*!< The count of opened file descriptor used to speed up computation */
} esp_remotefs_t;

// Possible error codes, these are negative to allow
// valid positive return values
enum lfs_error {
    LFS_ERR_OK          = 0,    // No error
    LFS_ERR_IO          = -5,   // Error during device operation
    LFS_ERR_CORRUPT     = -84,  // Corrupted
    LFS_ERR_NOENT       = -2,   // No directory entry
    LFS_ERR_EXIST       = -17,  // Entry already exists
    LFS_ERR_NOTDIR      = -20,  // Entry is not a dir
    LFS_ERR_ISDIR       = -21,  // Entry is a dir
    LFS_ERR_NOTEMPTY    = -39,  // Dir is not empty
    LFS_ERR_BADF        = -9,   // Bad file number
    LFS_ERR_FBIG        = -27,  // File too large
    LFS_ERR_INVAL       = -22,  // Invalid parameter
    LFS_ERR_NOSPC       = -28,  // No space left on device
    LFS_ERR_NOMEM       = -12,  // No more memory available
    LFS_ERR_NOATTR      = -61,  // No data/attr available
    LFS_ERR_NAMETOOLONG = -36,  // File name too long
};

// File open flags
enum lfs_open_flags {
    // open flags
    LFS_O_RDONLY = 1,         // Open a file as read only
#ifndef LFS_READONLY
    LFS_O_WRONLY = 2,         // Open a file as write only
    LFS_O_RDWR   = 3,         // Open a file as read and write
    LFS_O_CREAT  = 0x0100,    // Create a file if it does not exist
    LFS_O_EXCL   = 0x0200,    // Fail if a file already exists
    LFS_O_TRUNC  = 0x0400,    // Truncate the existing file to zero size
    LFS_O_APPEND = 0x0800,    // Move to end of file on every write
#endif

    // internally used flags
#ifndef LFS_READONLY
    LFS_F_DIRTY   = 0x010000, // File does not match storage
    LFS_F_WRITING = 0x020000, // File has been written since last flush
#endif
    LFS_F_READING = 0x040000, // File has been read since last flush
#ifndef LFS_READONLY
    LFS_F_ERRED   = 0x080000, // An error occurred during write
#endif
    LFS_F_INLINE  = 0x100000, // Currently inlined in directory entry
};

// File seek flags
enum lfs_whence_flags {
    LFS_SEEK_SET = 0,   // Seek relative to an absolute position
    LFS_SEEK_CUR = 1,   // Seek relative to the current file position
    LFS_SEEK_END = 2,   // Seek relative to the end of the file
};

/**
 * @brief Last Modified Time
 *
 * Use 't' for REMOTEFS_ATTR_MTIME to match example:
 *     https://github.com/ARMmbed/remotefs/issues/23#issuecomment-482293539
 * And to match other external tools such as:
 *     https://github.com/earlephilhower/mkremotefs
 */
#define REMOTEFS_ATTR_MTIME ((uint8_t) 't')

/**
 *Configuration structure for esp_vfs_remotefs_register.
 */
typedef struct {
    const char *base_path;            /**< Mounting point. */
    const char *partition_label;      /**< Label of partition to use. */
    uint8_t format_if_mount_failed:1; /**< Format the file system if it fails to mount. */
    uint8_t dont_mount:1;             /**< Don't attempt to mount or format. Overrides format_if_mount_failed */
} esp_vfs_remotefs_conf_t;

/**
 * Register and mount remotefs to VFS with given path prefix.
 *
 * @param   conf                      Pointer to esp_vfs_remotefs_conf_t configuration structure
 *
 * @return  
 *          - ESP_OK                  if success
 *          - ESP_ERR_NO_MEM          if objects could not be allocated
 *          - ESP_ERR_INVALID_STATE   if already mounted or partition is encrypted
 *          - ESP_ERR_NOT_FOUND       if partition for remotefs was not found
 *          - ESP_FAIL                if mount or format fails
 */
esp_err_t esp_vfs_remotefs_register(const esp_vfs_remotefs_conf_t * conf);

/**
 * Unregister and unmount remotefs from VFS
 *
 * @param partition_label  Label of the partition to unregister.
 *
 * @return  
 *          - ESP_OK if successful
 *          - ESP_ERR_INVALID_STATE already unregistered
 */
esp_err_t esp_vfs_remotefs_unregister(const char* partition_label);

/**
 * Check if remotefs is mounted
 *
 * @param partition_label  Label of the partition to check.
 *
 * @return  
 *          - true    if mounted
 *          - false   if not mounted
 */
bool esp_remotefs_mounted(const char* partition_label);

/**
 * Format the remotefs partition
 *
 * @param partition_label  Label of the partition to format.
 * @return  
 *          - ESP_OK      if successful
 *          - ESP_FAIL    on error
 */
esp_err_t esp_remotefs_format(const char* partition_label);

/**
 * Get information for remotefs
 *
 * @param partition_label           Optional, label of the partition to get info for.
 * @param[out] total_bytes          Size of the file system
 * @param[out] used_bytes           Current used bytes in the file system
 *
 * @return  
 *          - ESP_OK                  if success
 *          - ESP_ERR_INVALID_STATE   if not mounted
 */
esp_err_t esp_remotefs_info(const char* partition_label, size_t *total_bytes, size_t *used_bytes);

#if CONFIG_REMOTEFS_HUMAN_READABLE
/**
 * @brief converts an enumerated lfs error into a string.
 * @param lfs_errno The enumerated remotefs error.
 */
const char * esp_remotefs_errno(enum lfs_error lfs_errno);
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif
