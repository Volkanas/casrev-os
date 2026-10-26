#ifndef CASREV_VFS_H
#define CASREV_VFS_H

#include "types.h"

#define VFS_TYPE_FILE 1
#define VFS_TYPE_DIR  2

#define VFS_MAX_NAME 32
#define VFS_MAX_CONTENT 2048

typedef struct vfs_node {
    char name[VFS_MAX_NAME];
    uint32_t type;
    uint32_t size;
    char *content;
    struct vfs_node *parent;
    struct vfs_node *first_child;
    struct vfs_node *next_sibling;
} vfs_node_t;

void        vfs_init(void);
vfs_node_t *vfs_get_root(void);
vfs_node_t *vfs_get_cwd(void);
int         vfs_set_cwd(vfs_node_t *node);

vfs_node_t *vfs_find_child(vfs_node_t *parent, const char *name);
vfs_node_t *vfs_resolve_path(const char *path);
vfs_node_t *vfs_create_file(vfs_node_t *parent, const char *name, const char *initial_data);
vfs_node_t *vfs_create_dir(vfs_node_t *parent, const char *name);
int         vfs_write_file(vfs_node_t *node, const char *data, uint32_t len);
void        vfs_get_path(vfs_node_t *node, char *buf, uint32_t max_len);

#endif
