#! /bin/bash
# before running this  script -> turn off network-manager by  : sudo service network-manager stop
sudo ifconfig wlan0 up
sudo ip link set wlan0 down
sudo ip addr flush dev wlan0
sudo ip link set wlan0 up
# $1 is the mac address of  target AP
sudo iwconfig wlan0 essid STUDENTS-N ap 00:38:df:20:03:38
#sudo wpa_supplicant -Dnl80211 -i wlan2 -B -c /etc/wpa_supplicant/wpa_supplicant.conf
#sudo dhclient -v wlan2
# to turn on network-manager ->  sudo service network-manager  restart
