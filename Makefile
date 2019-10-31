
default:	build

clean:
	rm -rf Makefile objs

build:
	$(MAKE) -f objs/Makefile
	$(MAKE) -f objs/Makefile manpage

install:
	$(MAKE) -f objs/Makefile install

upgrade:
	/tmp/nginx-test/sbin/nginx -t

	kill -USR2 `cat /tmp/nginx-test/logs/nginx.pid`
	sleep 1
	test -f /tmp/nginx-test/logs/nginx.pid.oldbin

	kill -QUIT `cat /tmp/nginx-test/logs/nginx.pid.oldbin`
