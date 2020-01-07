build:
	gcc -w -o remoteServer remoteServer.c
	gcc -w -o remoteClient remoteClient.c

install: build
	./remoteServer 9008 1

clean:
	rm -r ./output.receive*
	killall [r]emoteServer
	killall [r]emoteClient
