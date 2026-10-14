/* Check connector compatibility and monitor identities without a display server. */
#undef NDEBUG
#define main vwl_main
#include "../vwl.c"
#undef main
#include "../output.c"
#include <assert.h>

int
main(void)
{
	struct wlr_output output = {
			.name = "DP-1",
			.make = "Dell Inc.",
			.model = "DELL U2720Q",
			.serial = "ABC1234",
	};
	char model[1024], identifier[1200];

	/* Existing connector rules retain their case-sensitive substring semantics. */
	assert(outputmatches(&output, "DP-1"));
	assert(outputmatches(&output, "DP-"));
	assert(outputmatches(&output, ""));
	assert(!outputmatches(&output, "dp-1"));
	assert(!outputmatches(&output, "HDMI-A-1"));
	output.name = "DP-10";
	assert(outputmatches(&output, "DP-1"));
	output.name = "eDP-1";
	assert(outputmatches(&output, "eDP-1"));

	assert(outputmatches(&output, "Dell Inc. DELL U2720Q ABC1234"));
	assert(outputmatches(&output, "DELL U2720Q"));
	assert(outputmatches(&output, "ABC1234"));
	/* A different cable or port must not change the monitor identity. */
	output.name = "HDMI-A-1";
	assert(outputmatches(&output, "HDMI-A-1"));
	assert(outputmatches(&output, "Dell Inc. DELL U2720Q ABC1234"));
	assert(!outputmatches(&output, "DP-1"));
	output.serial = "SECOND";
	assert(!outputmatches(&output, "ABC1234"));
	assert(outputmatches(&output, "DELL U2720Q SECOND"));

	/* A long model must not hide the serial or truncate a full-identity rule. */
	memset(model, 'M', sizeof(model) - 1);
	model[sizeof(model) - 1] = '\0';
	output.model = model;
	snprintf(identifier, sizeof(identifier), "Dell Inc. %s SECOND", model);
	assert(outputmatches(&output, "SECOND"));
	assert(outputmatches(&output, identifier));
	assert(!outputmatches(&output, "ABC1234"));

	/* Headless and nested outputs may omit any of these optional strings. */
	output.make = output.model = output.serial = NULL;
	assert(outputmatches(&output, "HDMI-A-1"));
	assert(outputmatches(&output, "Unknown Unknown Unknown"));
	assert(!outputmatches(&output, "ABC1234"));
	output.model = "Nested";
	assert(outputmatches(&output, "Unknown Nested Unknown"));
	output.make = output.model = output.serial = "";
	assert(!outputmatches(&output, "Unknown"));
	assert(outputmatches(&output, "HDMI-A-1"));

	puts("output rules: connectors, port changes, serials, long and missing identifiers passed");
	return 0;
}
