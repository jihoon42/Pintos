#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>

#include "devices/disk.h"
#include "filesys/off_t.h"

struct bitmap;

void inode_init(void);
bool inode_create(disk_sector_t, off_t, int32_t);
struct inode *inode_open(disk_sector_t);
struct inode *inode_reopen(struct inode *);
disk_sector_t inode_get_inumber(const struct inode *);
void inode_close(struct inode *);
void inode_remove(struct inode *);
off_t inode_read_at(struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at(struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write(struct inode *);
void inode_allow_write(struct inode *);
off_t inode_length(const struct inode *);

/** #Project 4: File System */
int32_t inode_get_type(const struct inode *);
bool inode_is_removed(const struct inode *);
void inode_set_link(struct inode *, const struct inode *);
struct inode *link_get_inode(struct inode *);
bool trans_link_target(struct inode *);
void trans_target_link(struct inode *, bool);

#endif /* filesys/inode.h */
