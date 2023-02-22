all:
	g++ solution.cpp -std=c++17 -lpthread -o program

clean:
	@rm -f program 
