# hpex47xled-LINUX
HP Mediasmart Server EX47x Hard Drive Bay LED Daemon

This is the initial release of this daemon. The out of tree kernel module - https://github.com/jcl-concept/EX470-led is great, but activates the drive bay on any 
drives activity. I wanted independent lighting of the bays based on I/O of the individual drives. This solves that problem. 

Requires Ubuntu 20.04 or above. May work on older versions. make install will place the hpex47xled file under /usr/local/bin and put the hpex47xled.service file in /etc/systemd/system. You'll need to enable the service via systemctl start hpex47xled.service && systemctl enable hpex47xled.service.

Please let me know if there are any issues. I cannot guarantee it will work if eSATA drives are connected. I do validate that drives are only on ata1 and ata2 - the two hosts that disks are attached to. YMMV.

This program builds on the work I did for the hpex47xled FreeBSD daemon. I read the /sys/* /stat file of each drive identified, pull in the read and write I/O and blink on difference. This is as close to real-time as I can get. If someone has a suggestion on how to read directly from the kernel or a better method, please let me know.

This version is threaded. One thread per disk and a threaded initialization. 

Comments/suggestions are welcome.
