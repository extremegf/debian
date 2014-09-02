/* Emacs linux kernel C mode:-*- linux-c -*-*/
/* 
 * Sterownik wypisuj��cy "You are the best!"
 *
 * Przed u��yciem nale��y utworzy�� pliki specjalne.
 *
 * Do odczytu wielokrotnego:
 * mknod /dev/yatb c 42 0  
 *
 * Do odczytu jednokrotnego:
 * mknod /dev/yatb_once c 42 1 
 *
 * Na podstawie: - Jon. Tombs Dec '93
 *
 */

/* Kernel includes */

#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/kernel.h> /* KERN_WARNING */

MODULE_LICENSE("GPL");

static const char yatb_reply[] = "You are the best!\n";
static long repeat=1;

/* Na sta��e wybrany numer g����wny - oficjalny numer pr��bny */
static int major = 42;

/*
 * Odczyt (z punktu widzenia aplikacji wo��aj��cej) to przepisanie danych z
 * przestrzeni adresowej j��dra do przestrzeni adresowej u��ytkownika.
 * Wersja jednokrotnego wypisania.
 */
static ssize_t yatb_once_read(struct file *file, char __user *buf, size_t count, loff_t *filepos)
{
	long my_pos = file->f_pos;
	long my_max = sizeof(yatb_reply) - 1;
	unsigned long not_copied;

	/* Czy to nie EOF? */
	if (my_pos >= my_max || my_pos < 0)
		return 0; 

	/* ustalamy, ile tak naprawd�� mo��emy skopiowa�� */
	if (count > my_max-my_pos)
		count = my_max-my_pos;

	/* kopiujemy do przestrzeni u��ytkownika */
	not_copied = copy_to_user(buf,yatb_reply+my_pos,count);

	if (not_copied != 0)
	{
		return -EFAULT;
	}

	/* copy_to_user zwraca liczb�� NIE skopiowanych bajt��w */
	my_pos += count;

	/* Uaktualnienie pozycji w pliku */
	*filepos = (loff_t)my_pos;

	/* Liczba przepisanych bajt��w */
	return count;
}

/*
 * Odczyt (z punktu widzenia aplikacji wo��aj��cej) to przepisanie danych z
 * przestrzeni adresowej j��dra do przestrzeni adresowej u��ytkownika.
 * Wersja z mo��liwo��ci�� wielokrotnego powt��rzenia napisu.
 *
 * Liczb�� powtorze�� przechowujemy w danych prywatnych otwartego pliku.
 */
static ssize_t yatb_read(struct file *file,char __user *buf, size_t count, loff_t *filepos)
{
	long my_pos = file->f_pos;
	int my_max = sizeof(yatb_reply) - 1;
	long *priv = file->private_data;
	long repeat = *priv;
	long my_avail = my_max * repeat - my_pos;
	long my_num = 0;
	long my_end = 0;

	/* Czy to nie EOF? */
	if (my_pos >= my_max*repeat || my_pos < 0)
		return 0; 

	/* ustalamy, ile tak naprawde mozemy skopiowac */
	if (my_avail > count)
		my_num = count;
	else
		my_num = my_avail;

	/* ustalamy, gdzie sko��czy�� odczyt */
	my_end = my_pos + my_num;
	
	/* 
	 * Tu zastosowano przepisanie bajt po bajcie, ale lepiej uzywa�� copy_to_user 
	 * (por. yatb_once_read)
	 */
	for (; my_pos < my_end; my_pos++) 
		if (put_user(yatb_reply[my_pos%my_max],buf++))
			return -EFAULT;

	/* Waktualnienie pozycji w pliku */
	*filepos = my_pos;

	/* Liczba przepisanych bajt��w */
	return my_num;
}

/*
 * Zapis (z punktu widzenia aplikacji wo��aj��cej) ma s��u��y�� ustaleniu
 * liczby powt��rze�� napisu przy odczycie (maksymalna pozycja w pliku osi��gni��ta
 * przy zapisie okre��la liczb�� powt��rze��)
 *
 * Liczb�� powt��rze�� przechowujemy jako dan�� prywatn�� otwartego pliku.
 */
static ssize_t yatb_write(struct file *file, const char __user *buf, size_t count, loff_t *filepos)
{
	long *priv = file->private_data;
	int my_max = sizeof(yatb_reply) - 1;
	long my_pos = *filepos + count;
	long my_size = my_pos * my_max;

	/* sprawdzenie, czy nowy rozmiar mie��ci si�� w zakresie long */
	if (my_size / my_max != my_pos || my_size < 0 || my_pos < 0)
		return -EINVAL;

	/* Waktualnienie pozycji w pliku */
	*filepos = my_pos;

	/* Zapamietanie obecnej pozycji - liczby powt��rzen */
	*priv = *filepos;

	printk(KERN_WARNING "Repeat set to: %ld\n", *priv);

	/* Liczba "przepisanych" bajt��w */
	return count;
}

static int yatb_open(struct inode *ino, struct file *filep);
static int yatb_release(struct inode *ino, struct file *filep);


/* operacje dla wersji z jednokrotnym wypisaniem */
static struct file_operations yatb_once_fops = {
	owner:		THIS_MODULE,
	read:		yatb_once_read,
	open:		yatb_open,
	release:	yatb_release,
};

/* operacje dla wersji z wielokrotnym wypisaniem */
static struct file_operations yatb_fops = {
	owner:		THIS_MODULE, 
	read:		yatb_read,
	write:		yatb_write,
	open:		yatb_open,
	release:	yatb_release,
};


/* Wsp��lny open i release */
static int
yatb_open(struct inode *ino, struct file *filep)
{

	/* Urz��dzenie o podnumerze 1 - odczyt jednokrotny */
	if (MINOR(ino->i_rdev) == 1)
			filep->f_op = &yatb_once_fops;
	else
	{
		/* Dla urz��dze�� z wielokrotnym wypisaniem (podnumery =0 lub >1)
		 * ustawienie domy��lnej liczby powt��rze�� na zmienn�� repeat (pocz��tkowo 1)
		 */
		filep->private_data = (void*)(&repeat);
	}
	
	return 0;   
}


/*
 * Funkcja zamykaj��ca - gdyby��my w funkcji open np. alokowali pami���� prywatn��
 * dla otwartego pliku, tu nale��a��oby j�� zwolni��.
 */
static int
yatb_release(struct inode *ino, struct file *filep)
{
	return 0;
}

/*
 * Inicjalizacja modu��u:
 * Rejestracja urz��dzenia znakowego.
 */
static int
yatb_init_module(void)
{
	/* standardowo rejestrujemy z zestawem instrukcji dla wielokrotnego wypisania */
	int err;
	if ((err = register_chrdev(major, "yatb", &yatb_fops))) {
		printk(KERN_WARNING "register_chrdev failed: You are NOT the best!\n");
		return err;
	}
	else /* Sukces! */
		printk(KERN_WARNING "Read from me!\n");

	return 0;
}
/*
 * Wyrejestrowanie urz��dzenia znakowego.
 */
static void
yatb_cleanup_module(void)
{
	unregister_chrdev(major, "yatb");
	printk(KERN_WARNING "unregister_chrdev succeeded\n");
}

module_init(yatb_init_module);
module_exit(yatb_cleanup_module);
