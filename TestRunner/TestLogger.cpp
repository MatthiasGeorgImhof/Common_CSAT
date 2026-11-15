#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <string>
#include <vector>
#include <sstream>
#include <functional>
#include "mock_hal.h"
#include <algorithm>
#include <fstream>

#include "Logger.hpp"

#ifdef LOGGER_OUTPUT_STDERR
TEST_CASE("Logger is enabled and outputs to STDERR")
{
  std::stringstream ss;
  Logger::setLogStream(&ss);

  SUBCASE("Log message at INFO level")
  {
    ss.str("");
    ss.clear();
    log(LOG_LEVEL_INFO, "This is a test info message: %d", 42);
    CHECK(ss.str() == "This is a test info message: 42\n");
  }

  SUBCASE("Log message at higher log level")
  {
    ss.str("");
    ss.clear();
    log(LOG_LEVEL_ERROR, "This is an error message: %s", "problem");
    CHECK(ss.str() == "This is an error message: problem\n");
  }

  SUBCASE("Log message at lower level - should NOT appear")
  {
    ss.str("");
    ss.clear();
    log(LOG_LEVEL_TRACE, "This should not appear");
    CHECK(ss.str() == "");
  }

  SUBCASE("Test multiple formats")
  {
    ss.str("");
    ss.clear();
    log(LOG_LEVEL_DEBUG, "Int: %d, Float: %f, String: %s", 100, 3.14, "test");
    CHECK(ss.str() == "Int: 100, Float: 3.140000, String: test\n");
  }

  SUBCASE("Log message to file")
  {
    std::ofstream logFile("test_log.txt"); // Use std::ofstream
    if (logFile.is_open())
    {
      Logger::setLogStream(&logFile); // Pass the address of the std::ofstream object
      log(LOG_LEVEL_ERROR, "This should be written to file: %f %s", 3.14f, "test");
      logFile.close();
    }

    std::ifstream file("test_log.txt");
    std::string line;
    std::string contents;
    if (file.is_open())
    {
      while (getline(file, line))
      {
        contents += line + "\n";
      }
      file.close();
    }
    CHECK(contents == "This should be written to file: 3.140000 test\n");
  }
}
#endif

#ifdef LOGGER_OUTPUT_UART
TEST_CASE("Test UART output")
{

  #ifdef LOGGER_OUTPUT_STDERR
  std::stringstream ss;
  Logger::setLogStream(&ss);
  #endif

  UART_HandleTypeDef huart;
  init_uart_handle(&huart);
  Logger::setUartHandle(&huart);

  SUBCASE("Log message at INFO level to UART")
  {
    #ifdef LOGGER_OUTPUT_STDERR
    ss.str("");
    ss.clear();
    #endif
    clear_uart_tx_buffer();
    log(LOG_LEVEL_INFO, "This is a UART message");
    CHECK(get_uart_tx_buffer_count() > 0);
    uint8_t *data = get_uart_tx_buffer();
    std::string str((char *)data);
    CHECK(str == "This is a UART message");
  }
  SUBCASE("Test multiple messages to UART")
  {
    #ifdef LOGGER_OUTPUT_STDERR
    ss.str("");
    ss.clear();
    #endif

    clear_uart_tx_buffer();
    log(LOG_LEVEL_INFO, "UART message one");
    log(LOG_LEVEL_INFO, "UART message two: %d", 123);
    CHECK(get_uart_tx_buffer_count() > 0);
    uint8_t *data = get_uart_tx_buffer();
    std::string str((char *)data);
    CHECK(str == "UART message oneUART message two: 123");
  }
}
#endif

#ifdef LOGGER_OUTPUT_USB
TEST_CASE("Test USB CDC output")
{
  #ifdef LOGGER_OUTPUT_STDERR
  std::stringstream ss;
  Logger::setLogStream(&ss);
  #endif
  
  SUBCASE("Log message to USB")
  {
    #ifdef LOGGER_OUTPUT_STDERR
    ss.str("");
    ss.clear();
    #endif
    clear_usb_tx_buffer();
    CHECK(get_usb_tx_buffer_count() == 0);

    log(LOG_LEVEL_INFO, "This is a USB CDC message");
    size_t len = get_usb_tx_buffer_count();
    CHECK(len > 0);
    uint8_t *data = get_usb_tx_buffer();
    std::string str((char *)data);
    CHECK(str == "This is a USB CDC message");
  }
}
#endif