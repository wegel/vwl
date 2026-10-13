#ifndef CAPTURE_RESULT_H
#define CAPTURE_RESULT_H
struct capture_pixels {
	int width, height, red, green, x, y, right, bottom;
};
struct capture_result {
	struct capture_pixels painted, clean, pointer;
	int entered, x, y, hotspot_x, hotspot_y, retries;
};
int capture_test_client(int socket_fd, int control_fd);
#endif
