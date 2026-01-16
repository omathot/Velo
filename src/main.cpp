import std;
import velo;

int main() {
	Velo app;
	try {
		app.run();
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
	return 0;
}
