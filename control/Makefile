minixdef=`if [ "\`uname\`" = "Minix" ]; then echo -D_MINIX; fi`

all: control

control: main.c
	cc $(minixdef) -Wall -o control main.c

clean:
	echo $(system)
	-rm control

