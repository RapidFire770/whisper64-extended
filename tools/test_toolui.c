int tool_reu_ok(unsigned long bytes);

int main(void) {
    if (tool_reu_ok(0))
        return 1;
    if (tool_reu_ok(65536ul))
        return 2;
    if (!tool_reu_ok(131072ul))
        return 3;
    if (!tool_reu_ok(16320ul * 1024ul))
        return 4;
    return 0;
}
