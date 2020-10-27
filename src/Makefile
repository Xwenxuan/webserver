http_conn.a:http_conn.o
	ar rcs libhttp_conn.a http_conn.o
threadpool.a:threadpool.o
	ar rcs libthtreadpool.a threadpool.o

http_conn.o :: http_conn.cpp
	g++ -c http_conn.cpp -o http_conn.o -I../include

threadpool.o:threadpool.cpp
	g++ -c threadpool.cpp -o threadpool.o -I../include

clean:
	rm -f *.o
	mv *.a ../lib