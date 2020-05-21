#include <linux/module.h>       /* Needed by all modules */
#include <linux/kernel.h>       /* Needed for KERN_INFO  */
#include <linux/init.h>         /* Needed for the macros */
#include <linux/fs.h>           /* libfs stuff           */
#include <linux/buffer_head.h>  /* buffer_head           */
#include <linux/slab.h>         /* kmem_cache            */
#include "assoofs.h"

/*
 *  Operaciones sobre ficheros
 */
ssize_t assoofs_read(struct file * filp, char __user * buf, size_t len, loff_t * ppos);
ssize_t assoofs_write(struct file * filp, const char __user * buf, size_t len, loff_t * ppos);
const struct file_operations assoofs_file_operations = {
    .read = assoofs_read,
    .write = assoofs_write,
};

ssize_t assoofs_read(struct file * filp, char __user * buf, size_t len, loff_t * ppos) {
    printk(KERN_INFO "Read request\n");
    return 0;
}

ssize_t assoofs_write(struct file * filp, const char __user * buf, size_t len, loff_t * ppos) {
    printk(KERN_INFO "Write request\n");
    return 0;
}

/*
 *  Operaciones sobre directorios
 */
static int assoofs_iterate(struct file *filp, struct dir_context *ctx);
const struct file_operations assoofs_dir_operations = {
    .owner = THIS_MODULE,
    .iterate = assoofs_iterate,
};

static int assoofs_iterate(struct file *filp, struct dir_context *ctx) {
    printk(KERN_INFO "Iterate request\n");
    return 0;
}

/*
 *  Operaciones sobre inodos
 */
static int assoofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);
struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags);
static int assoofs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);
static struct inode_operations assoofs_inode_ops = {
    .create = assoofs_create,
    .lookup = assoofs_lookup,
    .mkdir = assoofs_mkdir,
};

struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags) {
    printk(KERN_INFO "Lookup request\n");
    return NULL;
}


static int assoofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) {
    printk(KERN_INFO "New file request\n");
    return 0;
}

static int assoofs_mkdir(struct inode *dir , struct dentry *dentry, umode_t mode) {
    printk(KERN_INFO "New directory request\n");
    return 0;
}

/*
 *  Operaciones sobre el superbloque
 */
static const struct super_operations assoofs_sops = {
    .drop_inode = generic_delete_inode,
};


/*
 *  Funcion auxiliar nos permite obtener la informacion persistente del inodo numero inode_no del superbloque sb
 */
struct assoofs_inode_info *assoofs_get_inode_info(struct super_block *sb, uint64_t inode_no){
    //Accedemos a disco para leer el bloque que contiene el almacen de inodos
    struct assoofs_inode_info *inode_info = NULL;
    struct buffer_head *bh;
    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);   //Leemos bloque 1
    inode_info = (struct assoofs_inode_info *)bh->b_data;

    //Recorrer el almacen de inodos en busca del inodo inode_no
    struct assoofs_super_block_info *afs_sb = sb->s_fs_info;
    struct assoofs_inode_info *buffer = NULL;
    int i;
    for (i = 0; i < afs_sb->inodes_count; i++) {
        if (inode_info->inode_no == inode_no) {
            buffer = kmalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
            memcpy(buffer, inode_info, sizeof(*buffer));
            break;
        }
        inode_info++;
    }

    //Liberamos recursos y devolvemos la informacion del inodo inode_no, si estaba en el almacen
    brelse(bh);
    return buffer;
}

/*
 *  Inicialización del superbloque
 */
int assoofs_fill_super(struct super_block *sb, void *data, int silent) {   
    printk(KERN_INFO "assoofs_fill_super request\n");

    //Creacion de variables
    struct buffer_head *bh;
    struct assoofs_super_block_info *assoofs_sb; //sb en disco
    struct inode *root_inode;

    // 1.- Leer la información persistente del superbloque del dispositivo de bloques 
    bh = sb_bread(sb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER); //asignar a bh lo que nos devuelve sb_bread(sb en mem,cte primer bloque)
    assoofs_sb = (struct assoofs_super_block_info *)bh->b_data; //sacamos el contenido del superbloque y convertimos tipo var assoofs_sb
 
    // 2.- Comprobar los parámetros del superbloque
    if(assoofs_sb->magic!=ASSOOFS_MAGIC){
	printk(KERN_ERR "Magic Number mismatch\n");
    } 

    if(assoofs_sb->block_size!=ASSOOFS_DEFAULT_BLOCK_SIZE){
	printk(KERN_ERR "Block Size mismatch\n");
    }

    // 3.- Escribir la información persistente leída del dispositivo de bloques en el superbloque sb, incluído el campo s_op con las operaciones que soporta.
    sb->s_magic=ASSOOFS_MAGIC; //asignar num magic 
    sb->s_maxbytes=ASSOOFS_DEFAULT_BLOCK_SIZE;  //asiganr tam bloque
    sb->s_op=&assoofs_sops;  //asignar operaciones a sb
    sb->s_fs_info=assoofs_sb; //para no tener que acceder ctmt al bloque 0 del disco


    // 4.- Crear el inodo raíz y asignarle operaciones sobre inodos (i_op) y sobre directorios (i_fop)
    root_inode=new_inode(sb);
    inode_init_owner(root_inode, NULL, S_IFDIR); // S_IFDIR para directorios, S_IFREG para ficheros.
    root_inode->i_ino = ASSOOFS_ROOTDIR_INODE_NUMBER; // numero de inodo
    root_inode->i_sb = sb; // Puntero al superbloque
    root_inode->i_op = &assoofs_inode_ops; // direccion de una variable de tipo struct inode_operations previamente declarada
    root_inode->i_fop = &assoofs_dir_operations; /* 
                                                    direccion de una variable de tipo struct file_operations previamente          
                                                    declarada.En la practica tenemos 2: assoofs_dir_operations y assoofs_file_operations. 
                                                    La primera la utilizaremos cuando creemosinodos para directorios (como el directorio ra ́ız) y 
                                                    la segunda cuando creemos inodos para ficheros.
                                                  */
    root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime = current_time(root_inode); // Fechas
    root_inode->i_private = assoofs_get_inode_info(sb, ASSOOFS_ROOTDIR_INODE_NUMBER); // Informacion persistente del inodo

    //Al tratarse de un inodo raiz
    sb->s_root = d_make_root(root_inode);
    if(!sb->s_root){
        brelse(bh);
        return -ENOMEM;
    }

    brelse(bh); //LIberamos recursos
    return 0;
}


/*
 *  Montaje de dispositivos assoofs
 */
static struct dentry *assoofs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data) {
    printk(KERN_INFO "assoofs_mount request\n");
    struct dentry *ret = mount_bdev(fs_type, flags, dev_name, data, assoofs_fill_super);
    // Control de errores a partir del valor de ret. En este caso se puede utilizar la macro IS_ERR: if (IS_ERR(ret)) ...
    if(IS_ERR(ret)){
	   printk(KERN_ERR "Error while mounting ASSOOFS\n");
    }else{
	   printk(KERN_INFO "ASSOFS was succesfully mounted on: '%s'\n",dev_name);
    }
}

/*
 *  assoofs file system type
 */
static struct file_system_type assoofs_type = {
    .owner   = THIS_MODULE,
    .name    = "assoofs",
    .mount   = assoofs_mount,
    .kill_sb = kill_litter_super,
};

static int __init assoofs_init(void) {
    printk(KERN_INFO "assoofs_init request\n");
    int ret = register_filesystem(&assoofs_type);
    // Control de errores a partir del valor de ret
    if(ret==0){
	printk(KERN_INFO "Successfully registered ASSOOFS!\n");
    }else{
	printk(KERN_ERR "Fail ocurred while registering ASSOOFS. Error:[%d]",ret);
    }
}

static void __exit assoofs_exit(void) {
    printk(KERN_INFO "assoofs_exit request\n");
    int ret = unregister_filesystem(&assoofs_type);
    // Control de errores a partir del valor de ret
    if(ret==0){
	printk(KERN_INFO "Successfully unregistered ASSOOFS!\n");
    }else{
	printk(KERN_ERR "Fail ocurred while unregistering ASSOOFS. Error:[%d]",ret);
    }
}

module_init(assoofs_init);
module_exit(assoofs_exit);
