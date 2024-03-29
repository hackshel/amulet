

#define COLOR_BOLD       "\033[1m"
#define COLOR_DARK       "\033[2m"

#define COLOR_UNDERLINE  "\033[4m"
#define COLOR_BLINK      "\033[5m"

#define COLOR_REVERSE    "\033[7m"
#define COLOR_CONCEALED  "\033[8m"

#define COLOR_GREY       "\033[30m"
#define COLOR_RED        "\033[31m"
#define COLOR_GREEN      "\033[32m"
#define COLOR_YELLOW     "\033[33m"
#define COLOR_BLUE       "\033[34m"
#define COLOR_MAGENTA    "\033[35m"
#define COLOR_CYAN       "\033[36m"
#define COLOR_WHITE      "\033[37m"

#define COLOR_ON_GREY    "\033[40m"
#define COLOR_ON_RED     "\033[41m"
#define COLOR_ON_GREEN   "\033[42m"
#define COLOR_ON_YELLOW  "\033[43m"
#define COLOR_ON_BLUE    "\033[44m"
#define COLOR_ON_MAGENTA "\033[45m"
#define COLOR_ON_CYAN    "\033[46m"
#define COLOR_ON_WHITE   "\033[47m"

#define COLOR_RESET      "\033[0m"

#define COLOR( format, args )  args format COLOR_RESET

