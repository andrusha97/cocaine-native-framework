application: worker.o main.o logger.o
	g++ -o application worker.o main.o logger.o -lboost_system-mt -lgrapejson -lboost_program_options -lev -lmsgpack -luuid -lcrypto++

worker.o: worker.cpp
	g++ -std=c++0x -o worker.o -c worker.cpp

main.o: main.cpp
	g++ -std=c++0x -o main.o -c main.cpp
	
logger.o: logger.cpp
	g++ -std=c++0x -o logger.o -c logger.cpp