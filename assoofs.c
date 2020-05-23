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


/*
 *  Esta función permite mostrar el contenido de un directorio
 */
static int assoofs_iterate(struct file *filp, struct dir_context *ctx) {
    printk(KERN_INFO "Iterate request\n");

    //Acceder al inodo, a la información persistente del inodo, y al superbloque correspondientes al argumento filp
    struct inode *inode;
    struct super_block *sb;
    struct buffer_head *bh;
    loff_t pos;  //Long offset
    int i;
    struct assoofs_inode_info *inode_info;
    struct assoofs_dir_record_entry *record;

    inode = filp->f_path.dentry->d_inode; //Sacamos inodo en mem
    sb = inode->i_sb;  //Sacamos la info del superbloque
    inode_info = inode->i_private;  //Sacamos la parte persistente

    //Comprobar si el contexto del directorio ya está creado. Si no lo hacemos provocaremos un bucle infinito.
    //pos=ctx->pos;
    if (ctx->pos){ //Si es positivo, return 0
        return 0;
    } 

    //Hay que comprobar que el inodo obtenido en el paso 1 se corresponde con un directorio
    if ((!S_ISDIR(inode_info->mode))){
        printk(KERN_ERR "inode [%llu][%lu] for fs object not a directory\n",inode_info->inode_no,inode->i_ino);
        return -1;
    }

    //Accedemos al bloque donde se almacena el contenido del directorio y con la información que contiene inicializamos el contexto ctx:
    bh = sb_bread(sb, inode_info->data_block_number);  //Leemos el bloque
    record = (struct assoofs_dir_record_entry *)bh->b_data;
    for (i = 0; i < inode_info->dir_children_count; i++) {
        dir_emit(ctx, record->filename, ASSOOFS_FILENAME_MAXLEN, record->inode_no, DT_UNKNOWN); //Inicializar variables de ctx con valores del directorio
        ctx->pos += sizeof(struct assoofs_dir_record_entry);  //Incrementamos tanto como ucupe una variable dir_record_entry
        record++;
    }

    brelse(bh);

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

/*
 *  Funcion auxiliar nos permite obtener la informacion persistente del inodo numero inode_no del superbloque sb
 */
struct assoofs_inode_info *assoofs_get_inode_info(struct super_block *sb, uint64_t inode_no){
    //Accedemos a disco para leer el bloque que contiene el almacen de inodos
    struct assoofs_inode_info *inode_info = NULL;
    struct buffer_head *bh;
    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);   //Leemos bloque 1
    inode_info = (struct assoofs_inode_info *)bh->b_data; //Hacemos conversion

    //Recorrer el almacen de inodos en busca del inodo inode_no
    struct assoofs_super_block_info *afs_sb = sb->s_fs_info; //Idea no tener que acceder siempre al bloque 0, lo guardamos en esta variable
    struct assoofs_inode_info *buffer = NULL;
    int i;

    for (i = 0; i < afs_sb->inodes_count; i++) {      //Nada mas arrancar, num inodos es 2 (raiz y fich bienvenida)
        if (inode_info->inode_no == inode_no) {
            buffer = kmalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
            memcpy(buffer, inode_info, sizeof(*buffer));
            break;
        }
        inode_info++;   //++ en puntero: incrementar tantos bytes como ocupe una variable y pasa a la segunda
    }

    //Liberamos recursos y devolvemos la informacion del inodo inode_no, si estaba en el almacen
    brelse(bh);
    return buffer;
}

/*
 * Esta función auxiliar nos permitirá obtener un puntero al inodo número ino del superbloque sb.
 */
static struct inode *assoofs_get_inode(struct super_block *sb, int ino){
    struct inode *inode;
    struct assoofs_inode_info *inode_info;

    //Obtener la información persistente del inodo ino
    inode_info = assoofs_get_inode_info(sb, ino);

    //Asignamos
    inode=new_inode(sb);
    inode->i_ino = ino; 
    inode->i_sb = sb; 
    inode->i_op = &assoofs_inode_ops; 

    //Comprobamos valor f_op
    if (S_ISDIR(inode_info->mode))
        inode->i_fop = &assoofs_dir_operations;
    else if (S_ISREG(inode_info->mode))
        inode->i_fop = &assoofs_file_operations;
    else
        printk(KERN_ERR "Unknown inode type. Neither a directory nor a file.");

    //Demas
    inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode); 
    inode->i_private = inode_info;

    return inode;
}


/*
 *  Esta función busca la entrada (struct dentry) con el nombre correcto (child dentry->d name.name)
 *  en el directorio padre (parent inode). Se utiliza para recorrer y mantener el árbol de inodos.
 */

struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags) {
    printk(KERN_INFO "Lookup request\n");

    //Accedemos al bloque de disco con el contenido del directorio apuntado por parent inode
    struct assoofs_inode_info *parent_info = parent_inode->i_private;
    struct super_block *sb = parent_inode->i_sb;
    struct buffer_head *bh;
    int i;
    bh = sb_bread(sb, parent_info->data_block_number); 


    /*  Recorrer el contenido del directorio buscando la entrada cuyo nombre se corresponda con el que buscamos. Si se localiza
        la entrada, entonces tenemos construir el inodo correspondiente */
    struct assoofs_dir_record_entry *record;
    record = (struct assoofs_dir_record_entry *)bh->b_data;

    for (i=0; i < parent_info->dir_children_count; i++) {
        if (!strcmp(record->filename, child_dentry->d_name. name)) { //Se ejecuta cuando son iguales
            struct inode *inode = assoofs_get_inode(sb, record->inode_no); // Función auxiliar que obtine la información de un inodo a partir de su número de inodo
            inode_init_owner(inode, parent_inode, ((struct assoofs_inode_info *)inode->i_private)->mode);
            d_add(child_dentry, inode); //Construye arbol de inodos en mem
            return NULL;
        }
        record++;
    }

    printk(KERN_ERR "No inode found for the filename: [%s]\n", child_dentry->d_name.name);
    return NULL;
}


/*
 *  Esta función auxiliar nos permitirá obtener un puntero a la información persistente de un inodo concreto
 */
struct assoofs_inode_info *assoofs_search_inode_info(struct super_block *sb, struct assoofs_inode_info *start, struct assoofs_inode_info *search){
    uint64_t count = 0;
    
    while (start->inode_no != search->inode_no && count < ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_count) {
        count++;
        start++;
    }

    if(start->inode_no == search->inode_no){
        return start;
    }else{
        return NULL;
    }
}

/*
 *  Esta función auxiliar nos permitirá actualizar en disco la información persistente de un inodo:
 */
int assoofs_save_inode_info(struct super_block *sb, struct assoofs_inode_info *inode_info){
    struct buffer_head *bh;
    struct assoofs_inode_info *inode_pos;

    //Obtener de disco el almacén de inodos.
    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);

    //Buscar los datos de inode info en el almacén. Para ello se recomienda utilizar una función auxiliar
    inode_pos = assoofs_search_inode_info(sb, (struct assoofs_inode_info *)bh->b_data, inode_info);

    //Actualizar el inodo, marcar el bloque como sucio y sincronizar.
    memcpy(inode_pos, inode_info, sizeof(*inode_pos));
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    //brelse(bh);

    return 0;
}



/*
 *  Esta función auxiliar nos permitirá actualizar la información persistente del superbloque cuando hay un cambio
 */
void assoofs_save_sb_info(struct super_block *vsb){
    //Leer de disco la información persistente del superbloque con sb bread y sobreescribir el campo b_data con la informacion en memoria:
    struct buffer_head *bh; //Para grabar en disco
    struct assoofs_super_block *sb = vsb->s_fs_info; // Información persistente del superbloque en memoria
    bh = sb_bread(vsb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER); //Accedemos a disco
    bh->b_data = (char *)sb; // Sobreescribo los datos de disco con la información en memoria

    //Para que el cambio pase a disco, basta con marcar el buffer como sucio y sincronizar
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);
}


/*
 *  Esta función auxiliar nos permitirá obtener un bloque libre:
 */
int assoofs_sb_get_a_freeblock(struct super_block *sb, uint64_t *block){
    struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;
    int i;

    for (i = 2; i < ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED; i++) //Desde 2, pues super y alm inodos (bloque 0 y 1)
        if (assoofs_sb->free_blocks & (1 << i)) //comprobar bit del indice esta libre o no
            break; // cuando aparece el primer bit 1 en free_block dejamos de recorrer el mapa de bits, i tiene la posición del primer bloque libre
        
    *block = i; // Escribimos el valor de i en la dirección de memoria indicada como segundo argumento en la función

    if(i== ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED){
        printk(KERN_ERR "There are no more free blocks avalible\n");
        return -1;
    }

    //Por último, hay que actualizar el valor de free blocks y guardar los cambios en el superbloque.
    assoofs_sb->free_blocks &= ~(1 << i);
    assoofs_save_sb_info(sb);
    return 0;
    
}


/*
 *  Esta función auxiliar nos permitirá guardar en disco la información persistente de un inodo nuevo
 */
void assoofs_add_inode_info(struct super_block *sb, struct assoofs_inode_info *inode){
    struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;
    uint64_t count;
    struct buffer_head *bh;
    struct assoofs_inode_info *inode_info;

    //Acceder a la información persistente del superbloque (sb->s fs info) para obtener el contador de inodos (inodes count).
    count = ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_count;

    //Leer de disco el bloque que contiene el almacén de inodos.
    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER); 

    //Obtener un puntero al final del almacén y escribir un nuevo valor al final.
    inode_info = (struct assoofs_inode_info *)bh->b_data;
    inode_info += assoofs_sb->inodes_count;
    memcpy(inode_info, inode, sizeof(struct assoofs_inode_info));   //Copia de mem la info del inodo y cuantos bytes se copian

    //Para que los cambios persistan
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);


    //Actualizar el contador de inodos de la informacion persistente del superbloque y guardar los cambios.
    assoofs_sb->inodes_count++;
    assoofs_save_sb_info(sb);

    brelse(bh);
}


static int assoofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) {
    printk(KERN_INFO "New file request\n");

    //Creamos un inodo, con algunas consideraciones:
    struct inode *inode;
    uint64_t count;
    struct assoofs_inode_info *inode_info;

    struct super_block *sb;
    struct buffer_head *bh;

    struct assoofs_inode_info *parent_inode_info;
    struct assoofs_dir_record_entry *dir_contents;

    /* ==== PARTE 1: ==== */
    sb = dir->i_sb; // obtengo un puntero al superbloque desde dir
    count = ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_count; // obtengo el número de inodos de la información persistente del superbloque
    inode = new_inode(sb);
    inode->i_ino = count + 1; // Asigno número al nuevo inodo a partir de count

    if(count >= ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED){
        printk(KERN_ERR "Max number of objects supported by ASSOOFS has been reached\n");
        return -1;
    }
 
    /*  Hay que guardar en el campo i private la información persistente del mismo (struct assoofs inode info). 
        En este caso, no llamo a assoofs get inode info, se trata de un nuevo inodo y tengo que crearlo desde cero  */
    inode_info = kmalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
    inode_info->inode_no = inode->i_ino;
    inode_info->mode = mode; // El segundo mode me llega como argumento
    inode_info->file_size = 0;
    inode->i_private = inode_info;
    inode_init_owner(inode, dir, mode);
    d_add(dentry, inode);

    //Para las operaciones sobre ficheros
    inode->i_fop=&assoofs_file_operations;

    //Hay que asignarle un bloque al nuevo inodo, por lo que habrá que consultar el mapa de bits del superbloque.
    assoofs_sb_get_a_freeblock(sb, &inode_info->data_block_number); //Direccion donde se escribe el bloque del fichero

    //Guardar la información persistente del nuevo inodo en disco
    assoofs_add_inode_info(sb, inode_info);


    /* ==== PARTE 2: ===== */
    //Modificar el contenido del directorio padre, añadiendo una nueva entrada para el nuevo archivo o directorio. El nombre lo sacaremos del segundo parámetro.
    parent_inode_info = dir->i_private; //Sacamos info persistente del padre 
    bh = sb_bread(sb, parent_inode_info->data_block_number);  //Leemos el contenido del disco del bloque del dir padre

    dir_contents = (struct assoofs_dir_record_entry *)bh->b_data;
    dir_contents += parent_inode_info->dir_children_count; //Avanzar el puntero para llegar al final (fig, tercer bloque, final amarillo)
    dir_contents->inode_no = inode_info->inode_no; // inode_info es la información persistente del inodo creado en el paso 2.

    strcpy(dir_contents->filename, dentry->d_name.name);
    mark_buffer_dirty(bh);  //Marcar como sucio
    sync_dirty_buffer(bh);  //Volcar todos los cambios en bh a disco
    brelse(bh);


    /* ===== PÀRTE 3: ===== */
    //Actualizar la información persistente del inodo padre indicando que ahora tiene un archivo más.
    parent_inode_info->dir_children_count++;
    assoofs_save_inode_info(sb, parent_inode_info); 

    return 0;
}

/*
 *  Esta función nos permitirá crear nuevos inodos para directorios.
 */
static int assoofs_mkdir(struct inode *dir , struct dentry *dentry, umode_t mode) {
    printk(KERN_INFO "New directory request\n");

    //Creamos un inodo, con algunas consideraciones:
    struct inode *inode;
    uint64_t count;
    struct assoofs_inode_info *inode_info;

    struct super_block *sb;
    struct buffer_head *bh;

    struct assoofs_inode_info *parent_inode_info;
    struct assoofs_dir_record_entry *dir_contents;

    /* ==== PARTE 1: ==== */
    sb = dir->i_sb; // obtengo un puntero al superbloque desde dir
    count = ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_count; // obtengo el número de inodos de la información persistente del superbloque
    inode = new_inode(sb);
    inode->i_ino = count + 1; // Asigno número al nuevo inodo a partir de count

    if(count >= ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED){
        printk(KERN_ERR "Max number of objects supported by ASSOOFS has been reached\n");
        return -1;
    }
 
    /*  Hay que guardar en el campo i private la información persistente del mismo (struct assoofs inode info). 
        En este caso, no llamo a assoofs get inode info, se trata de un nuevo inodo y tengo que crearlo desde cero  */
    inode_info = kmalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
    inode_info->inode_no = inode->i_ino;
    inode_info->mode = S_IFDIR | mode; //CAMBIO
    inode_info->dir_children_count = 0;  //CAMBIO
    inode->i_private = inode_info;
    inode_init_owner(inode, dir, mode);
    d_add(dentry, inode);

    //Para las operaciones sobre directorios
    inode->i_fop=&assoofs_dir_operations;  //CAMBIO

    //Hay que asignarle un bloque al nuevo inodo, por lo que habrá que consultar el mapa de bits del superbloque.
    assoofs_sb_get_a_freeblock(sb, &inode_info->data_block_number); //Direccion donde se escribe el bloque del fichero

    //Guardar la información persistente del nuevo inodo en disco
    assoofs_add_inode_info(sb, inode_info);


    /* ==== PARTE 2: ===== */
    //Modificar el contenido del directorio padre, añadiendo una nueva entrada para el nuevo archivo o directorio. El nombre lo sacaremos del segundo parámetro.
    parent_inode_info = dir->i_private; //Sacamos info persistente del padre 
    bh = sb_bread(sb, parent_inode_info->data_block_number);  //Leemos el contenido del disco del bloque del dir padre

    dir_contents = (struct assoofs_dir_record_entry *)bh->b_data;
    dir_contents += parent_inode_info->dir_children_count; //Avanzar el puntero para llegar al final (fig, tercer bloque, final amarillo)
    dir_contents->inode_no = inode_info->inode_no; // inode_info es la información persistente del inodo creado en el paso 2.

    strcpy(dir_contents->filename, dentry->d_name.name);
    mark_buffer_dirty(bh);  //Marcar como sucio
    sync_dirty_buffer(bh);  //Volcar todos los cambios en bh a disco
    brelse(bh);


    /* ===== PÀRTE 3: ===== */
    //Actualizar la información persistente del inodo padre indicando que ahora tiene un archivo más.
    parent_inode_info->dir_children_count++;
    assoofs_save_inode_info(sb, parent_inode_info); 

    return 0;
}

/*
 *  Operaciones sobre el superbloque
 */
static const struct super_operations assoofs_sops = {
    .drop_inode = generic_delete_inode,
};

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
       brelse(bh);
       return -1;
    } 

    if(assoofs_sb->block_size!=ASSOOFS_DEFAULT_BLOCK_SIZE){
       printk(KERN_ERR "Block Size mismatch\n");
       brelse(bh);
       return -1;
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
        return -1;
    }

    //LIberamos recursos
    brelse(bh); 
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
       return NULL;
    }else{
       printk(KERN_INFO "ASSOFS was succesfully mounted on: '%s'\n",dev_name);
    }

    return ret;
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
        return -1;
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
