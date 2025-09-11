#include <unistd.h>

int main(void) {
    closefrom(3);
    return 0;
}
