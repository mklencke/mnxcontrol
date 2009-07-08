all:
	cd nc && make
	cd control && make

clean:
	cd nc && make clean
	cd control && make clean

