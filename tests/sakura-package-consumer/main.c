#include <sakura/sakura-control-client.h>

int
main(void)
{
	return SAKURA_CONTROL_CLIENT_API_VERSION == 1 ? 0 : 1;
}
