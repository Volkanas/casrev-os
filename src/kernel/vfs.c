#include "vfs.h"
#include "heap.h"
#include "string.h"

static vfs_node_t *root_node = 0;
static vfs_node_t *current_dir = 0;

static vfs_node_t *alloc_node(const char *name, uint32_t type, vfs_node_t *parent)
{
    vfs_node_t *node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (!node) return 0;

    memset(node, 0, sizeof(vfs_node_t));
    strcpy(node->name, name);
    node->type = type;
    node->parent = parent;

    if (parent) {
        if (!parent->first_child) {
            parent->first_child = node;
        } else {
            vfs_node_t *cur = parent->first_child;
            while (cur->next_sibling) {
                cur = cur->next_sibling;
            }
            cur->next_sibling = node;
        }
    }

    return node;
}

void vfs_init(void)
{
    root_node = alloc_node("/", VFS_TYPE_DIR, 0);
    current_dir = root_node;

    /* Ornek Klasorler */
    vfs_node_t *belgeler = vfs_create_dir(root_node, "belgeler");
    vfs_node_t *notlar   = vfs_create_dir(root_node, "notlar");
    vfs_node_t *sistem   = vfs_create_dir(root_node, "sistem");

    /* Ornek Dosyalar */
    vfs_create_file(root_node, "readme.txt",
        "casrev-os v0.9 Sanal Dosya Sistemi (RamFS)\n"
        "Hos geldiniz! Bu dosya sistemi bellek uzerinde calisir.\n"
        "Dosya Gezgini ve Not Defteri ile dosyalari acabilir ve duzenleyebilirsiniz.\n");

    if (belgeler) {
        vfs_create_file(belgeler, "gorevler.txt",
            "1. VFS ve Dosya Yoneticisi\n"
            "2. Not Defteri ile metin duzenleme\n"
            "3. Paint ile serbest cizim\n"
            "4. Mayin Tarlasi oyunu\n");
    }

    if (notlar) {
        vfs_create_file(notlar, "gunluk.txt",
            "Bugun casrev-os icin grafik arayuz ve dosya sistemi eklendi.\n"
            "Masaustu pencereleri cok akici calisiyor!\n");
    }

    if (sistem) {
        vfs_create_file(sistem, "surum.txt",
            "casrev-os v0.9\n"
            "Mimari: x86 32-bit Korumali Mod\n"
            "Video: VESA Mode 0x114 (800x600x16, RGB565)\n");
    }
}

vfs_node_t *vfs_get_root(void)
{
    return root_node;
}

vfs_node_t *vfs_get_cwd(void)
{
    return current_dir ? current_dir : root_node;
}

int vfs_set_cwd(vfs_node_t *node)
{
    if (!node || node->type != VFS_TYPE_DIR) return 0;
    current_dir = node;
    return 1;
}

vfs_node_t *vfs_find_child(vfs_node_t *parent, const char *name)
{
    if (!parent || parent->type != VFS_TYPE_DIR || !name) return 0;

    if (strcmp(name, ".") == 0) return parent;
    if (strcmp(name, "..") == 0) return parent->parent ? parent->parent : parent;

    vfs_node_t *child = parent->first_child;
    while (child) {
        if (strcmp(child->name, name) == 0) {
            return child;
        }
        child = child->next_sibling;
    }
    return 0;
}

vfs_node_t *vfs_create_file(vfs_node_t *parent, const char *name, const char *initial_data)
{
    if (!parent || parent->type != VFS_TYPE_DIR || !name) return 0;

    /* Ayni isimde dosya var mi? */
    vfs_node_t *existing = vfs_find_child(parent, name);
    if (existing) {
        if (initial_data) {
            vfs_write_file(existing, initial_data, strlen(initial_data));
        }
        return existing;
    }

    vfs_node_t *node = alloc_node(name, VFS_TYPE_FILE, parent);
    if (!node) return 0;

    node->content = (char *)kmalloc(VFS_MAX_CONTENT);
    if (node->content) {
        memset(node->content, 0, VFS_MAX_CONTENT);
        if (initial_data) {
            uint32_t len = strlen(initial_data);
            if (len >= VFS_MAX_CONTENT) len = VFS_MAX_CONTENT - 1;
            memcpy(node->content, initial_data, len);
            node->content[len] = '\0';
            node->size = len;
        } else {
            node->size = 0;
        }
    }

    return node;
}

vfs_node_t *vfs_create_dir(vfs_node_t *parent, const char *name)
{
    if (!parent || parent->type != VFS_TYPE_DIR || !name) return 0;

    vfs_node_t *existing = vfs_find_child(parent, name);
    if (existing) return existing;

    return alloc_node(name, VFS_TYPE_DIR, parent);
}

int vfs_write_file(vfs_node_t *node, const char *data, uint32_t len)
{
    if (!node || node->type != VFS_TYPE_FILE) return 0;

    if (!node->content) {
        node->content = (char *)kmalloc(VFS_MAX_CONTENT);
        if (!node->content) return 0;
    }

    if (len >= VFS_MAX_CONTENT) len = VFS_MAX_CONTENT - 1;
    memcpy(node->content, data, len);
    node->content[len] = '\0';
    node->size = len;
    return 1;
}

vfs_node_t *vfs_resolve_path(const char *path)
{
    if (!path || !*path) return current_dir;

    vfs_node_t *cur = (path[0] == '/') ? root_node : (current_dir ? current_dir : root_node);

    char part[VFS_MAX_NAME];
    const char *p = path;
    if (*p == '/') p++;

    while (*p) {
        int i = 0;
        while (*p && *p != '/' && i < VFS_MAX_NAME - 1) {
            part[i++] = *p++;
        }
        part[i] = '\0';

        if (*p == '/') p++;

        if (part[0] == '\0') continue;

        cur = vfs_find_child(cur, part);
        if (!cur) return 0;
    }

    return cur;
}

void vfs_get_path(vfs_node_t *node, char *buf, uint32_t max_len)
{
    if (!buf || max_len == 0) return;
    if (!node || node == root_node) {
        strcpy(buf, "/");
        return;
    }

    /* Ust dizinleri bul ve geriye dogru birlestir */
    vfs_node_t *chain[16];
    int depth = 0;
    vfs_node_t *cur = node;

    while (cur && cur != root_node && depth < 16) {
        chain[depth++] = cur;
        cur = cur->parent;
    }

    buf[0] = '\0';
    uint32_t cur_len = 0;

    for (int i = depth - 1; i >= 0; i--) {
        if (cur_len + 1 < max_len) {
            buf[cur_len++] = '/';
            buf[cur_len] = '\0';
        }
        uint32_t nlen = strlen(chain[i]->name);
        for (uint32_t j = 0; j < nlen && cur_len + 1 < max_len; j++) {
            buf[cur_len++] = chain[i]->name[j];
        }
        buf[cur_len] = '\0';
    }
}
