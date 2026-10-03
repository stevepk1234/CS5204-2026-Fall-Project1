#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "xv6-riscv/kernel/fs.h"
#include "xv6-riscv/kernel/types.h"
#include "xv6-riscv/kernel/param.h"

// Directory entries per block
#define DPB (BSIZE / sizeof(struct dirent)) 

#define die(msg) do { fprintf(stderr, "ERROR: %s\n", msg); exit(1); } while (0)

char * image;

struct superblock sb;
int addresses[FSSIZE];

struct dinode * get_inode(uint inum) {
    return (struct dinode *)(image + sb.inodestart * BSIZE + inum * sizeof(struct dinode));
}

char * get_block(uint block_num) {
    return image + block_num * BSIZE;
}

// Assumption: inode type is T_DIR
ushort dirent_inum_from_block(char * name, uint address) {
    struct dirent * entries = (struct dirent *) get_block(address);
    ushort inum_found = 0;

    for ( uint i = 0; i < DPB; i++ ) {
        struct dirent * entry = &entries[i];

        if ( entry->inum == 0 ) {
            continue;
        }

        if ( strncmp(entry->name, name, DIRSIZ) == 0 ) {
            inum_found = entry->inum;
            break;
        }
    }

    return inum_found;
}

ushort find_dirent_by_inum(uint inum, uint address) {
    struct dirent * entries = (struct dirent *) get_block(address);
    ushort inum_found = 0;

    for ( uint i = 0; i < DPB; i++ ) {
        struct dirent * entry = &entries[i];

        if ( entry->inum == 0 ) {
            continue;
        }

        if ( entry->inum == inum ) {
            inum_found = entry->inum;
            break;
        }
    }

    return inum_found;
}

/**
    [11] Each directory must only appear in one other directory only.
    ERROR: directory appears more than once in file system
*/
int check_directory_tree() {
    uint parents[sb.ninodes];

    for (uint i = 0; i < sb.ninodes; i++) {
        parents[i] = 0;
    }

    for (uint i = 0; i < sb.ninodes; i++) {
        struct dinode * inode = get_inode(i);

        if (inode->type == T_DIR) {
            for ( uint j = 0; j < NDIRECT; j++) {
                if (inode->addrs[j] == 0) {
                    continue;
                }

                struct dirent * entries = (struct dirent *) get_block(inode->addrs[j]);
                for ( uint k = 0; k < DPB; k++) {
                    struct dirent * entry = &entries[k];

                    if ( entry->inum == 0 ) {
                        continue;
                    }

                    struct dinode * entry_inode = get_inode(entry->inum);
                    if ( entry_inode->type == T_DIR && parents[entry->inum] != 0 ) {
                        if ( strncmp(entry->name, ".", DIRSIZ) != 0 && strncmp(entry->name, "..", DIRSIZ) != 0 ) {
                            return 1;
                        }
                    }

                    parents[entry->inum] = 1;
                }
            }

            if ( inode->addrs[NDIRECT] != 0 ) {
                uint * indirect_block = (uint *) get_block(inode->addrs[NDIRECT]);

                for ( uint j = 0; j < NINDIRECT; j++) {
                    if (indirect_block[j] == 0) {
                        continue;
                    }

                    struct dirent * entries = (struct dirent *) get_block(indirect_block[j]);
                    for ( uint k = 0; k < DPB; k++) {
                        struct dirent * entry = &entries[k];

                        if ( entry->inum == 0 ) {
                            continue;
                        }

                        struct dinode * entry_inode = get_inode(entry->inum);
                        if ( entry_inode->type == T_DIR && parents[entry->inum] != 0 ) {
                            if ( strncmp(entry->name, ".", DIRSIZ) != 0 && strncmp(entry->name, "..", DIRSIZ) != 0 ) {
                                return 1;
                            }
                        }

                        parents[entry->inum] = 1;
                    }
                }
            }
        }
    }

    return 0;
}

/**
    [10] Each inode that is referred to must be marked in use.
    ERROR: inode referred to in directory but marked free.
*/
int check_inode_free_directory() {
    for (uint i = 0; i < sb.ninodes; i++) {
        struct dinode * inode = get_inode(i);

        if (inode->type == T_DIR) {
            for ( uint j = 0; j < NDIRECT; j++) {
                if (inode->addrs[j] == 0) {
                    continue;
                }

                struct dirent * entries = (struct dirent *) get_block(inode->addrs[j]);
                for ( uint k = 0; k < DPB; k++) {
                    struct dirent * entry = &entries[k];

                    if ( entry->inum == 0 ) {
                        continue;
                    }

                    struct dinode * entry_inode = get_inode(entry->inum);
                    if ( entry_inode->type == 0 ) {
                        return 1;
                    }
                }
            }

            if ( inode->addrs[NDIRECT] != 0 ) {
                uint * indirect_block = (uint *) get_block(inode->addrs[NDIRECT]);
                for ( uint j = 0; j < NINDIRECT; j++) {
                    if (indirect_block[j] == 0) {
                        continue;
                    }

                    struct dirent * entries = (struct dirent *) get_block(indirect_block[j]);
                    for ( uint k = 0; k < DPB; k++) {
                        struct dirent * entry = &entries[k];

                        if ( entry->inum == 0 ) {
                            continue;
                        }

                        struct dinode * entry_inode = get_inode(entry->inum);
                        if ( entry_inode->type == 0 ) {
                            return 1;
                        }
                    }
                }
            }
        }
    }
    
    return 0;
}

/**
    [9] Each in-use inode must be referred to in at least one directory.
    ERROR: inode marked use but not found in a directory
*/
int check_inode_inuse_directory() {
    for (uint i = 0; i < sb.ninodes; i++) {
        struct dinode * inode = get_inode(i);

        if (inode->type != 0) {
            ushort found_in_dir = 0;

            for ( uint j = 0; j < sb.ninodes; j++) {
                struct dinode * dir_inode = get_inode(j);

                if (dir_inode->type == T_DIR) {
                    for ( uint k = 0; k < NDIRECT; k++) {
                        if (dir_inode->addrs[k] == 0) {
                            continue;
                        }

                        ushort inum_found = find_dirent_by_inum(i, dir_inode->addrs[k]);
                        if ( inum_found == i ) {
                            found_in_dir = 1;
                            break;
                        }
                    }

                    if ( dir_inode->addrs[NDIRECT] != 0 ) {
                        uint * indirect_block = (uint *) get_block(dir_inode->addrs[NDIRECT]);
                        for ( uint k = 0; k < NINDIRECT; k++) {
                            if (indirect_block[k] == 0) {
                                continue;
                            }

                            ushort inum_found = find_dirent_by_inum(i, indirect_block[k]);
                            if ( inum_found == i ) {
                                found_in_dir = 1;
                                break;
                            }
                        }
                    }
                }

                if ( found_in_dir == 1 ) {
                    break;
                }
            }

            if ( found_in_dir == 0 ) {
                return 1;
            }
        }
    }

    return 0;
}

/**
    [8] Reference count of each inode must match the number of times a file is referred to in directories.
    ERROR: bad reference count for file
*/
int check_inode_reference_count() {
    for (uint i = 0; i < sb.ninodes; i++) {
        struct dinode * inode = get_inode(i);

        if (inode->type != 0) {
            ushort ref_count = 0;

            for ( uint j = 0; j < sb.ninodes; j++) {
                struct dinode * dir_inode = get_inode(j);

                if (dir_inode->type == T_DIR) {
                    for ( uint k = 0; k < NDIRECT; k++) {
                        if (dir_inode->addrs[k] == 0) {
                            continue;
                        }

                        ushort inum_found = find_dirent_by_inum(i, dir_inode->addrs[k]);
                        if ( inum_found == i ) {
                            ref_count++;
                        }
                    }

                    if ( dir_inode->addrs[NDIRECT] != 0 ) {
                        uint * indirect_block = (uint *) get_block(dir_inode->addrs[NDIRECT]);
                        for ( uint k = 0; k < NINDIRECT; k++) {
                            if (indirect_block[k] == 0) {
                                continue;
                            }

                            ushort inum_found = find_dirent_by_inum(i, indirect_block[k]);
                            if ( inum_found == i ) {
                                ref_count++;
                            }
                        }
                    }
                }
            }

            if ( ref_count != inode->nlink ) {
                return 1;
            }
        }
    }

    return 0;
}

/**
    [7] Check each block in image. In-use/free blocks must be consistent with bitmap.
    ERROR: bitmap marks block in use but it is not in use
    ERROR: address used by inode but marked free in bitmap
*/
int check_bitmap_consistency() {
    for ( int i = sb.bmapstart + 1; i < FSSIZE; i++ ) {
        int bblock = sb.bmapstart + (i / BPB);
        int boffset = i % BPB;
        
        char * bmap_block = get_block(bblock);

        int byte_index = boffset / 8;
        int bit_index = boffset % 8;

        uint in_use = (bmap_block[byte_index] >> bit_index) & 1;

        if ( in_use == 1 && addresses[i] == 0 ) {
            return 1;
        }

        if ( in_use == 0 && addresses[i] == 1 ) {
            return 2;
        }    
    }

    return 0;
}

/**
    [6] Each directory contains . and .. entries. The . entry points to the directory itself.
    ERROR: directory not properly formatted
*/
int check_directory_entries() {
    for ( uint i = 0; i < sb.ninodes; i++) {
        struct dinode * inode = get_inode(i);

        if (inode->type == T_DIR) {
            ushort dot_found = 0;
            ushort ddot_found = 0;

            for ( uint j = 0; j < NDIRECT; j++) {
                if (inode->addrs[j] == 0) {
                    continue;
                }

                dot_found = dirent_inum_from_block(".", inode->addrs[j]);
                ddot_found = dirent_inum_from_block("..", inode->addrs[j]);

                if ( ddot_found != 0 && ddot_found != 0 ) {
                    if ( dot_found != i ) {
                        return 1;
                    }
                    return 0;
                }
            }

            if ( inode->addrs[NDIRECT] != 0 ) {
                uint * indirect_block = (uint *) get_block(inode->addrs[NDIRECT]);
                for ( uint j = 0; j < NINDIRECT; j++) {
                    if (indirect_block[j] == 0) {
                        continue;
                    }

                    dot_found = dirent_inum_from_block(".", indirect_block[j]);
                    ddot_found = dirent_inum_from_block("..", indirect_block[j]);

                    if ( ddot_found != 0 && ddot_found != 0 ) {
                        if ( dot_found != i ) {
                            return 1;
                        }
                        return 0;
                    }
                }
            }
        }
    }
    return 1;
}

/**
    [5] Check that for each inode, each address is only used once.
    ERROR: [in]direct address used more than once
*/
int check_inode_address_count() {
    for (uint i = 0; i < sb.ninodes; i++) {
        struct dinode * inode = get_inode(i);

        if (inode->type != 0) {
            for (uint j = 0; j < NDIRECT; j++) {
                if ( inode->addrs[j] == 0 ) {
                    continue;
                }

                if ( addresses[inode->addrs[j]] == 1 ) {
                    return 1;
                }

                addresses[inode->addrs[j]] = 1;
            }

            if ( inode->addrs[NDIRECT] != 0 ) {
                addresses[inode->addrs[NDIRECT]] = 1;

                uint * indirect_block = (uint *) get_block(inode->addrs[NDIRECT]);
                for (uint j = 0; j < NINDIRECT; j++) {
                    if (indirect_block[j] == 0) {
                        continue;
                    }

                    if (addresses[indirect_block[j]] == 1) {
                        return 2;
                    }

                    addresses[indirect_block[j]] = 1;
                }
            }
        }
    }
    return 0;
}

/**
    [4] Check that all inodes have valid addresses.
    ERROR: bad [in]direct address in inode
*/
int check_inode_address_valid() {
    uint data_block_start = sb.bmapstart + 1;
    uint data_block_end = sb.size - 1;

    for (uint i = 0; i < sb.ninodes; i++) {
        struct dinode * inode = get_inode(i);

        if (inode->type == 0) {
            continue;
        }

        for (uint j = 0; j < NDIRECT; j++) {
            if (inode->addrs[j] == 0) {
                continue;
            }

            if (inode->addrs[j] < data_block_start || inode->addrs[j] > data_block_end) {
                return 1;
            }
        }

        if ( inode->addrs[NDIRECT] != 0 ) {
            uint * indirect_block = (uint *) get_block(inode->addrs[NDIRECT]);
            for (uint j = 0; j < NINDIRECT; j++) {
                if (indirect_block[j] == 0) {
                    continue;
                }

                if (indirect_block[j] < data_block_start || indirect_block[j] > data_block_end) {
                    return 2;
                }
            }
        }
    }

    return 0;
}

/**
    [3] Check that all inodes have a valid type.
    ERROR: bad inode
*/
int check_inode_type() {
    for (uint i = 0; i < sb.ninodes; i++) {
        struct dinode * inode = get_inode(i);
        if (inode->type != 0 && inode->type != T_DIR && inode->type != T_FILE && inode->type != T_DEVICE) {
            return 1;
        }
    }
    return 0;
}

/**
    [2] Check that root inode is a directory.
    ERROR: root directory does not exist
*/
int check_root_directory() {
    struct dinode * root = get_inode(ROOTINO);
    if (root->type != T_DIR) {
        return 1;
    }
    return 0;
}

/**
    [1] Checks for superblock consistency. Returns 0 if consistent, 1 if inconsistent.
    ERROR: bad superblock
*/
int check_superblock() {
    sb = *(struct superblock *) get_block(1);

    if (sb.magic != FSMAGIC) {
        return 1;
    }

    if (sb.size != FSSIZE) {
        return 1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    // Validate number of args
    if (argc != 2) {
        printf("usage: xcheck [xv6 filesystem image]\n");
        exit(1);
    }

    // Open the filesystem image
    char *fs_img = argv[optind];
    int fd = open(fs_img, O_RDONLY);
    if (fd == -1) {
		die("file open failed");
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        close(fd);
        die("fstat failed");
    }

    size_t size = st.st_size;

    // Create memory mapping
    image = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (image == MAP_FAILED) {
        close(fd);
        die("mmap failed");
    }

    for (int i = 0; i < FSSIZE; i++) {
        addresses[i] = 0;
    }

    int rc = 0;

    if ( check_superblock() != 0) {
        munmap(image, size);
        close(fd);
        die("bad superblock");
    }

    if ( check_root_directory() != 0) {
        munmap(image, size);
        close(fd);
        die("root directory does not exist");
    }

    if ( check_inode_type() != 0) {
        munmap(image, size);
        close(fd);
        die("bad inode");
    }

    if ( (rc = check_inode_address_valid()) != 0) {
        munmap(image, size);
        close(fd);
        if ( rc == 1 ) {
            die("bad direct address in inode");
        }
        else {
            die("bad indirect address in inode");
        }
    }

    if ( (rc = check_inode_address_count()) != 0) {
        munmap(image, size);
        close(fd);
        if ( rc == 1 ) {
            die("direct address used more than once");
        }
        else {
            die("indirect address used more than once");
        }
    }

    if ( check_directory_entries() != 0) {
        munmap(image, size);
        close(fd);
        die("directory not properly formatted");
    }

    if ( (rc = check_bitmap_consistency()) != 0) {
        munmap(image, size);
        close(fd);
        if ( rc == 1 ) {
            die("bitmap marks block in use but it is not in use");
        }
        else {
            die("address used by inode but marked free in bitmap");
        }
    }

    if ( check_inode_reference_count() != 0) {
        munmap(image, size);
        close(fd);
        die("bad reference count for file");
    }

    if ( check_inode_inuse_directory() != 0) {
        munmap(image, size);
        close(fd);
        die("inode marked use but not found in a directory");
    }

    if ( check_inode_free_directory() != 0) {
        munmap(image, size);
        close(fd);
        die("inode referred to in directory but marked free");
    }

    if ( check_directory_tree() != 0) {
        munmap(image, size);
        close(fd);
        die("directory appears more than once in file system");
    }

    munmap(image, size);
    close(fd);
    return 0;
}
