#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "assoofs.h"

#define WELCOMEFILE_DATABLOCK_NUMBER (ASSOOFS_LAST_RESERVED_BLOCK + 1)
#define WELCOMEFILE_INODE_NUMBER (ASSOOFS_LAST_RESERVED_INODE + 1)

static int write_superblock(int fd) { //recibe descriptor
    struct assoofs_super_block_info sb = {
        .version = 1,
        .magic = ASSOOFS_MAGIC,
        .block_size = ASSOOFS_DEFAULT_BLOCK_SIZE,
        .inodes_count = WELCOMEFILE_INODE_NUMBER,
        .free_blocks = (~0) & ~(15), //1 bloque libre, 0 ocupado
    };
    ssize_t ret;

    ret = write(fd, &sb, sizeof(sb));  //escribir superbloque
    if (ret != ASSOOFS_DEFAULT_BLOCK_SIZE) {
        printf("Bytes written [%d] are not equal to the default block size.\n", (int)ret);
        return -1;
    }

    printf("Super block written succesfully.\n");
    return 0;
}

static int write_root_inode(int fd) {  //inodo raiz
    ssize_t ret;

    struct assoofs_inode_info root_inode;

    root_inode.mode = S_IFDIR;	//directorio
    root_inode.inode_no = ASSOOFS_ROOTDIR_INODE_NUMBER;
    root_inode.data_block_number = ASSOOFS_ROOTDIR_BLOCK_NUMBER;
    root_inode.dir_children_count = 1;	//archivos tiene dentro (readme.txt)

    ret = write(fd, &root_inode, sizeof(root_inode));	//escribimos

    if (ret != sizeof(root_inode)) {
        printf("The inode store was not written properly.\n");
        return -1;
    }

    printf("root directory inode written succesfully.\n");
    return 0;
}

static int write_welcome_inode(int fd, const struct assoofs_inode_info *i) { //fichero bienvenida
    off_t nbytes;
    ssize_t ret;

    ret = write(fd, i, sizeof(*i)); //escribimos
    if (ret != sizeof(*i)) {
        printf("The welcomefile inode was not written properly.\n");
        return -1;
    }
    printf("welcomefile inode written succesfully.\n");

    nbytes = ASSOOFS_DEFAULT_BLOCK_SIZE - (sizeof(*i) * 2); //meter espacio en blanco: tam bloque - inodo raiz (2,root y bienvenida)
    ret = lseek(fd, nbytes, SEEK_CUR);
    if (ret == (off_t)-1) {
        printf("The padding bytes are not written properly.\n");
        return -1;
    }

    printf("inode store padding bytes (after two inodes) written sucessfully.\n");
    return 0;
}

int write_dirent(int fd, const struct assoofs_dir_record_entry *record) { //entrada de directorio
    ssize_t nbytes = sizeof(*record), ret;

    ret = write(fd, record, nbytes);
    if (ret != nbytes) {
        printf("Writing the rootdirectory datablock (name+inode_no pair for welcomefile) has failed.\n");
        return -1;
    }
    printf("root directory datablocks (name+inode_no pair for welcomefile) written succesfully.\n");

    nbytes = ASSOOFS_DEFAULT_BLOCK_SIZE - sizeof(*record); //anyadimos relleno: bloque - tam entrada
    ret = lseek(fd, nbytes, SEEK_CUR);
    if (ret == (off_t)-1) {
        printf("Writing the padding for rootdirectory children datablock has failed.\n");
        return -1;
    }
    printf("Padding after the rootdirectory children written succesfully.\n");
    return 0;
}

int write_block(int fd, char *block, size_t len) { //escribir el contenido del fichero en cuestion
    ssize_t ret;

    ret = write(fd, block, len);
    if (ret != len) {
        printf("Writing file body has failed.\n");
        return -1;
    }
    printf("block has been written succesfully.\n");
    return 0;
}

int main(int argc, char *argv[])
{
    int fd;
    ssize_t ret;
    char welcomefile_body[] = "Hola mundo, os saludo desde un sistema de ficheros ASSOOFS.\n"; //mensaje
    
    struct assoofs_inode_info welcome = {  //i-nodo bienvenida
        .mode = S_IFREG,
        .inode_no = WELCOMEFILE_INODE_NUMBER,
        .data_block_number = WELCOMEFILE_DATABLOCK_NUMBER,
        .file_size = sizeof(welcomefile_body),
    };
    
    struct assoofs_dir_record_entry record = { //entrada fichero
        .filename = "README.txt",
        .inode_no = WELCOMEFILE_INODE_NUMBER,
    };

    if (argc != 2) {
        printf("Usage: mkassoofs <device>\n");
        return -1;
    }

    fd = open(argv[1], O_RDWR);
    if (fd == -1) {
        perror("Error opening the device");
        return -1;
    }

    ret = 1;
    do {
        if (write_superblock(fd))
            break;

        if (write_root_inode(fd))
            break;
        
        if (write_welcome_inode(fd, &welcome)) //inoo hemos creado
            break;

        if (write_dirent(fd, &record)) //entrada de directorio
            break;
        
        if (write_block(fd, welcomefile_body, welcome.file_size)) //pasamos contenido y tam del fichero
            break;

        ret = 0;
    } while (0);

    close(fd);
    return ret;
}
