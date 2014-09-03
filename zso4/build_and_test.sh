git pull
make 2>&1 | python error_repath.py
rmmod transdb
insmod transdb.ko