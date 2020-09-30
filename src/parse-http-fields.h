#ifndef PARSE_HTTP_FIELDS_H
#define PARSE_HTTP_FIELDS_H

struct httpparser;
struct httpheader;

int
http_parse_host(const struct httpparser *p, struct httpheader *hdr, unsigned char c);

#endif

