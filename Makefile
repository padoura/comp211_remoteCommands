PYTHON ?= /usr/bin/python
PREFIX ?= /Users/mpantourakis/Documents/connect-vpn-cli
#/usr

build:
	gcc -o remoteServer remoteServer.c
	gcc -o remoteServer2 remoteServer2.c
	gcc -o remoteClient remoteClient.c
	gcc -o remoteClient2 remoteClient2.c

install: build
# 	cp f5vpn-login.py $(PREFIX)/sbin/f5vpn-login.py
# 	chmod +x $(PREFIX)/sbin/f5vpn-login.py
# 	cp f5vpn-login-runner $(PREFIX)/bin/f5vpn-login
# 	chmod u+s $(PREFIX)/bin/f5vpn-login
# 	cp f5vpn-login-term $(PREFIX)/bin/f5vpn-login-term
# 	chmod +x $(PREFIX)/bin/f5vpn-login-term
# 	cp f5vpn-login.png $(PREFIX)/share/pixmaps/f5vpn-login.png
# 	cp f5vpn-login.desktop $(PREFIX)/share/applications/f5vpn-login.desktop

# clean:
# 	rm f5vpn-login-runner
