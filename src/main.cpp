#include <iostream>

// import std;
import velo;

int main(int argc, char** /*argv*/) {
	Velo app;
	if (argc > 1) {
		std::cout << "Setting codam\n";
		app.set_codam_mode();
	}
	try {
		app.run();
	} catch (const std::exception& e) {
		std::cerr << e.what() << std::endl;
		return 1;
	}
	return 0;
}
