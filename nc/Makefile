minixdef=`if [ "\`uname\`" = "Minix" ]; then echo -D_MINIX; fi`

all: nc

nc: main.c
	cc $(minixdef) -Wall -o nc main.c

clean:
	-rm nc

