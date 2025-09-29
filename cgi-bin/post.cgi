#!/bin/bash
echo "HTTP/1.1 200 OK"
echo "Content-Type: text/html"
echo ""
echo "<h1>Hello from CGI</h1>"
read body
echo "<h2>request body:</h2>"
echo "<p>"
echo $body
echo "</p>"