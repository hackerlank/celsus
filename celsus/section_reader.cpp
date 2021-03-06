#include "stdafx.h"
#include "celsus.hpp"
#include "section_reader.hpp"
#include "text_scanner.hpp"

namespace
{
  const char *is_section_header(const char *buf, const char *buf_end, int *len)
  {
    return scan_get_between(buf, buf_end, '[', ']', len);
  }

}

SectionReader::SectionReader()
  : _cur_section(NULL)
  , _buf(NULL)
  , _buf_len(0)
  , _section_ofs(0)
{
}

SectionReader::~SectionReader()
{
  SAFE_ADELETE(_buf);
}

bool SectionReader::load(const char *filename, const char *section)
{
  SAFE_ADELETE(_buf);

  HANDLE h = CreateFileA(filename, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
  if (h == INVALID_HANDLE_VALUE)
    return false;

  DWORD hi, _buf_len = GetFileSize(h, &hi);
  _buf = new char[_buf_len];
  DWORD bytes_read;
  if (!ReadFile(h, (void*)_buf, _buf_len, &bytes_read, NULL))
    return false;
  CloseHandle(h);

  // load all the sections
  const char *cur = _buf;
  const char *buf_end = _buf + _buf_len;
  std::string section_header;
  std::vector< std::string > rows;
  int len = 0;
  while (cur && (cur = scan_read_line(cur, buf_end, &len)) != NULL) {
    if (len > 0) {
      int len2 = 0;
      if (const char *h = is_section_header(cur, cur + len, &len2)) {
        // store the previous section header
        if (!rows.empty()) {
          _sections.insert(std::make_pair(section_header, rows));
          rows.clear();
        }
        section_header = std::string(h, len2);
      } else {
        rows.push_back(std::string(cur, len));
      }
    }
    cur = scan_skip_line(cur, buf_end);
  }

  if (!rows.empty())
    _sections.insert(std::make_pair(section_header, rows));

  bool res = true;
  if (section)
    res &= load_section(section);

  return res;
}

bool SectionReader::load_section(const char *section)
{
  _cur_section = NULL;
  _section_ofs = 0;

  auto it = _sections.find(section);
  if (it == _sections.end())
    return false;

  _cur_section = &(it->second);
  return true;
}


bool SectionReader::init_cur_section(const char **buf, const char **buf_end)
{
  if (!_cur_section || _section_ofs >= (int)_cur_section->size())
    return false;

  *buf = (*_cur_section)[_section_ofs].c_str();
  *buf_end = *buf + (*_cur_section)[_section_ofs].size();
  _section_ofs++;
  return true;
}

bool SectionReader::read_floats_inner(int count, float* out)
{
  const char *buf, *buf_end;
  if (!init_cur_section(&buf, &buf_end))
    return false;

  std::vector<float> f;
  parse_floats(buf, buf_end, &f);
  if ((int)f.size() != count)
    return false;

  for (int i = 0; i < count; ++i)
    *out++ = f[i];

  return true;
}

bool SectionReader::read_ints_inner(int count, int *out)
{
  const char *buf, *buf_end;
  if (!init_cur_section(&buf, &buf_end))
    return false;

  std::vector<int> v;
  parse_ints(buf, buf_end, &v);
  if ((int)v.size() != count)
    return false;

  for (int i = 0; i < count; ++i)
    *out++ = v[i];

  return true;
}

bool SectionReader::read_string(std::string *out)
{
  const char *buf, *buf_end;
  if (!init_cur_section(&buf, &buf_end))
    return false;

  int len;
  if (!scan_read_line(buf, buf_end, &len))
    return false;
  *out = std::string(buf, len);

  return true;
}

bool SectionReader::read_float(float *out)
{
  return read_floats_inner(1, out);
}

bool SectionReader::read_int(int *out)
{
  return read_ints_inner(1, out);
}

bool SectionReader::read_vector3(D3DXVECTOR3 *out)
{
  return read_floats_inner(3, &out->x);
}

bool SectionReader::read_color(D3DXCOLOR *out)
{
  return read_floats_inner(4, &out->r);
}
