#include <stdlib.h>
#include <check.h>
#include "test_suites/namedprops_mysql.h"
#include "test_suites/indexing.h"



int main(void)
{
	int number_failed;
	SRunner *sr = srunner_create(suite_create ("Open Change unit tests"));

	srunner_add_suite(sr, namedprops_mysql_suite());
	srunner_add_suite(sr, indexing_suite());

	srunner_set_xml(sr, "test_results.xml");
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}