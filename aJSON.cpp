/*
 Copyright (c) 2001, Interactive Matter, Marcus Nowotny

 Based on the cJSON Library, Copyright (C) 2009 Dave Gamble

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 */

// aJSON
// aJson Library for Arduino.
// This library is suited for Atmega328 based Arduinos.
// The RAM on ATmega168 based Arduinos is too limited

/******************************************************************************
 * Includes
 ******************************************************************************/

#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <float.h>
#include <limits.h>
#include <ctype.h>
#include <avr/pgmspace.h>
#include "aJSON.h"

/******************************************************************************
 * Definitions
 ******************************************************************************/
//Default buffer sizes - buffers get initialized and grow acc to that size
#define BUFFER_DEFAULT_SIZE 4

// Internal constructor.
aJsonObject*
aJsonClass::newItem()
{
  aJsonObject* node = (aJsonObject*) malloc(sizeof(aJsonObject));
  if (node)
    memset(node, 0, sizeof(aJsonObject));
  return node;
}

// Delete a aJsonObject structure.
void
aJsonClass::deleteItem(aJsonObject *c)
{
  aJsonObject *next;
  while (c)
    {
      next = c->next;
      if (!(c->type & aJson_IsReference) && c->child)
        deleteItem(c->child);
      if ((c->type == aJson_String) && c->value.valuestring)
        free(c->value.valuestring);
      if (c->name)
        free(c->name);
      free(c);
      c = next;
    }
}

// Parse the input text to generate a number, and populate the result into item.
int
aJsonClass::parseNumber(aJsonObject *item, FILE* stream)
{
  int i = 0;
  char sign = 1;

  int in = fgetc(stream);
  if (in == EOF)
    {
      return EOF;
    }

  // It is easier to decode ourselves than to use sscnaf, since so we can easier decide btw
  // int & float
  if (in == '-')
    {
      //it is a negative number
      sign = -1;
      in = fgetc(stream);
      if (in == EOF)
        {
          return EOF;
        }
    }
  if (in >= '0' && in <= '9')
    do
      {
        i = (i * 10.0) + (in - '0');
        in = fgetc(stream);
      }
    while (in >= '0' && in <= '9'); // Number?
  //end of integer part � or isn't it?
  if (!(in == '.' || in == 'e' || in == 'E'))
    {
      item->value.valueint = i * (int) sign;
      item->type = aJson_Int;
    }
  //ok it seems to be a float
  else
    {
      float n = (float) i;
      unsigned char scale = 0;
      int subscale = 0;
      char signsubscale = 1;
      if (in == '.')
        {
          in = fgetc(stream);
          do
            {
              n = (n * 10.0) + (in - '0'), scale--;
              in = fgetc(stream);
            }
          while (in >= '0' && in <= '9');
        } // Fractional part?
      if (in == 'e' || in == 'E') // Exponent?
        {
          in = fgetc(stream);
          if (in == '+')
            {
              in = fgetc(stream);
            }
          else if (in == '-')
            {
              signsubscale = -1;
              in = fgetc(stream);
            }
          while (in >= '0' && in <= '9')
            {
              subscale = (subscale * 10) + (in - '0'); // Number?
              in = fgetc(stream);
            }
        }

      n = sign * n * pow(10.0, ((float) scale + (float) subscale
          * (float) signsubscale)); // number = +/- number.fraction * 10^+/- exponent

      item->value.valuefloat = n;
      item->type = aJson_Float;
    }
  return 0;
}

// Render the number nicely from the given item into a string.
char*
aJsonClass::printInt(aJsonObject *item)
{
  char *str;
  str = (char*) malloc(21); // 2^64+1 can be represented in 21 chars.
  if (str)
    sprintf_P(str, PSTR("%d"), item->value.valueint);

  return str;
}

char*
aJsonClass::printFloat(aJsonObject *item)
{
  char *str;
  float d = item->value.valuefloat;
  str = (char*) malloc(64); // This is a nice tradeoff.
  if (str)
    {
      if (fabs(floor(d) - d) <= DBL_EPSILON)
        sprintf_P(str, PSTR("%.0f"), d);
      else if (fabs(d) < 1.0e-6 || fabs(d) > 1.0e9)
        sprintf_P(str, PSTR("%e"), d);
      else
        sprintf_P(str, PSTR("%f"), d);
    }
  return str;
}

// Parse the input text into an unescaped cstring, and populate item.
static const unsigned char firstByteMark[7] =
  { 0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC };

int
aJsonClass::parseString(aJsonObject *item, FILE* stream)
{
  //we do not need to skip here since the first byte should be '\"'
  int in = fgetc(stream);
  if (in != '\"')
    {
      return EOF; // not a string!
    }
  //allocate a buffer & track how long it is and how much we have read
  char* buffer = (char*) malloc(BUFFER_DEFAULT_SIZE * sizeof(char));
  if (buffer == NULL)
    {
      //unable to allocate the string
      return EOF;
    }
  in = fgetc(stream);
  if (in == EOF)
    {
      free(buffer);
      return EOF;
    }
  unsigned int buffer_length = BUFFER_DEFAULT_SIZE;
  unsigned int buffer_bytes = 0;
  while (in != EOF)
    {
      while (in != '\"' && in > 31)
        {
          if (in != '\\')
            buffer = addToBuffer((char) in, buffer, &buffer_length,
                &buffer_bytes);
          else
            {
              in = fgetc(stream);
              if (in == EOF)
                {
                  free(buffer);
                  return EOF;
                }
              switch (in)
                {
              case 'b':
                buffer = addToBuffer('\b', buffer, &buffer_length,
                    &buffer_bytes);
                break;
              case 'f':
                buffer = addToBuffer('\f', buffer, &buffer_length,
                    &buffer_bytes);
                break;
              case 'n':
                buffer = addToBuffer('\n', buffer, &buffer_length,
                    &buffer_bytes);
                break;
              case 'r':
                buffer = addToBuffer('\r', buffer, &buffer_length,
                    &buffer_bytes);
                break;
              case 't':
                buffer = addToBuffer('\t', buffer, &buffer_length,
                    &buffer_bytes);
                break;
              default:
                //we do not understand it so we skip it
                break;
                }
            }
          in = fgetc(stream);
          if (in == EOF)
            {
              free(buffer);
              return EOF;
            }
        }
      //the string ends here
      buffer = addToBuffer(0, buffer, &buffer_length, &buffer_bytes);
      if (in == EOF)
        {
          free(buffer);
          return EOF;
        }
      //trim the buffer
      buffer = (char*) realloc(buffer, buffer_bytes);
      item->value.valuestring = buffer;
      item->type = aJson_String;
      return 0;
    }
  //we should not be here but it is ok
  return 0;
}

// Render the cstring provided to an escaped version that can be printed.
char*
aJsonClass::printStringPtr(const char *str)
{
  const char *ptr;
  char *ptr2, *out;
  int len = 0;

  if (!str)
    return strdup("");
  ptr = str;
  while (*ptr && ++len)
    {
      if ((unsigned char) *ptr < 32 || *ptr == '\"' || *ptr == '\\')
        len++;
      ptr++;
    }

  out = (char*) malloc(len + 3);
  if (!out)
    return 0;

  ptr2 = out;
  ptr = str;
  *ptr2++ = '\"';
  while (*ptr)
    {
      if ((unsigned char) *ptr > 31 && *ptr != '\"' && *ptr != '\\')
        *ptr2++ = *ptr++;
      else
        {
          *ptr2++ = '\\';
          switch (*ptr++)
            {
          case '\\':
            *ptr2++ = '\\';
            break;
          case '\"':
            *ptr2++ = '\"';
            break;
          case '\b':
            *ptr2++ = 'b';
            break;
          case '\f':
            *ptr2++ = 'f';
            break;
          case '\n':
            *ptr2++ = 'n';
            break;
          case '\r':
            *ptr2++ = 'r';
            break;
          case '\t':
            *ptr2++ = 't';
            break;
          default:
            ptr2--;
            break; // eviscerate with prejudice.
            }
        }
    }
  *ptr2++ = '\"';
  *ptr2++ = 0;
  return out;
}

// Invote print_string_ptr (which is useful) on an item.
char*
aJsonClass::printString(aJsonObject *item)
{
  return printStringPtr(item->value.valuestring);
}

// Utility to jump whitespace and cr/lf
int
aJsonClass::skip(FILE* stream)
{
  if (stream == NULL)
    {
      return EOF;
    }
  int in = fgetc(stream);
  while (in != EOF && (in <= 32))
    {
      in = fgetc(stream);
    }
  if (in != EOF)
    {
      if (ungetc(in, stream) == EOF)
        {
          return EOF;
        }
      return 0;
    }
  return EOF;
}

// Parse an object - create a new root, and populate.
aJsonObject*
aJsonClass::parse(char *value)
{
}

// Parse an object - create a new root, and populate.
aJsonObject*
aJsonClass::parse(FILE* stream)
{
}

// Parse an object - create a new root, and populate.
aJsonObject*
aJsonClass::parse(FILE* stream, char** filter)
{
  if (stream == NULL)
    {
      return NULL;
    }
  aJsonObject *c = newItem();
  if (!c)
    return NULL; /* memory fail */

  skip(stream);
  if (!parseValue(c, stream))
    {
      deleteItem(c);
      return NULL;
    }
  return c;
}

// Render a aJsonObject item/entity/structure to text.
int
aJsonClass::print(aJsonObject* item, FILE* stream)
{
  return printValue(item, FILE* stream);
}

// Parser core - when encountering text, process appropriately.
int
aJsonClass::parseValue(aJsonObject *item, FILE* stream)
{
  if (stream == NULL)
    {
      return EOF; // Fail on null.
    }
  //TODO is it better to skip here?
  if (skip(stream))
    {
      return EOF;
    }
  //read the first byte from the stream
  int in = fgetc(stream);
  if (in == EOF)
    {
      return EOF;
    }
  if (ungetc(in, stream) == EOF)
    {
      return EOF;
    }
  if (in == '\"')
    {
      return parseString(item, stream);
    }
  else if (in == '-' || (in >= '0' && in <= '9'))
    {
      return parseNumber(item, stream);
    }
  else if (in == '[')
    {
      return parseArray(item, stream);
    }
  else if (in == '{')
    {
      return parseObject(item, stream);
    }
  //it can only be null, false or true
  else if (in == 'n')
    {
      //a buffer to read the value
      char buffer[] =
        { 0, 0, 0, 0 };
      if (fread(buffer, sizeof(char), 4, stream) != 4)
        {
          return EOF;
        }
      if (!strncmp(buffer, "null", 4))
        {
          item->type = aJson_NULL;
          return 0;
        }
      else
        {
          return EOF;
        }
    }
  else if (in == 'f')
    {
      //a buffer to read the value
      char buffer[] =
        { 0, 0, 0, 0, 0 };
      if (fread(buffer, sizeof(char), 5, stream) != 5)
        {
          return EOF;
        }
      if (!strncmp(buffer, "false", 5))
        {
          item->type = aJson_False;
          item->value.valuebool = 0;
          return 0;
        }
    }
  else if (in == 't')
    {
      //a buffer to read the value
      char buffer[] =
        { 0, 0, 0, 0 };
      if (fread(buffer, sizeof(char), 4, stream) != 4)
        {
          return EOF;
        }
      if (!strncmp(buffer, "true", 4))
        {
          item->type = aJson_True;
          item->value.valuebool = 1;
          return 0;
        }
    }

  return EOF; // failure.
}

// Render a value to text.
int
aJsonClass::printValue(aJsonObject *item, FILE* stream)
{
  int result = 0;
  if (!item)
    {
      //nothing to do
      return 0;
    }
  switch (item->type)
    {
  case aJson_NULL:
    result = fprintf_P(stream, PSTR("null"));
    break;
  case aJson_False:
    result = fprintf_P(stream, PSTR("false"));
    break;
  case aJson_True:
    result = fprintf_P(stream, PSTR("true"));
    break;
  case aJson_Int:
    result = printInt(item, stream);
    break;
  case aJson_Float:
    result = printFloat(item, stream);
    break;
  case aJson_String:
    result = printString(item, stream);
    break;
  case aJson_Array:
    result = printArray(item, stream);
    break;
  case aJson_Object:
    result = printObject(item, stream);
    break;
    }
  return result;
}

// Build an array from input text.
int
aJsonClass::parseArray(aJsonObject *item, FILE* stream)
{
  aJsonObject *child;
  int in = fgetc(stream);
  if (in != '[')
    {
      return EOF; // not an array!
    }

  item->type = aJson_Array;
  skip(stream);
  in = fgetc(stream);
  if (in == ']')
    {
      return 0; // empty array.
    }
  //now put back the last character
  if (ungetc(in, stream) == EOF)
    {
      return EOF;
    }
  item->child = child = newItem();
  if (item->child == NULL)
    {
      return EOF; // memory fail
    }
  skip(stream);
  if (parseValue(child, stream))
    {
      return EOF;
    }
  skip(stream);
  in = fgetc(stream);
  while (in == ',')
    {
      aJsonObject *new_item = newItem();
      if (new_item == NULL)
        {
          return EOF; // memory fail
        }
      child->next = new_item;
      new_item->prev = child;
      child = new_item;
      skip(stream);
      if (parseValue(child, stream))
        {
          return EOF;
        }
      skip(stream);
      in = fgetc(stream);
    }
  return 0; // malformed.
}

// Render an array to text
char*
aJsonClass::printArray(aJsonObject *item)
{
  char **entries;
  char *out = 0, *ptr, *ret;
  int len = 5;
  aJsonObject *child = item->child;
  unsigned char numentries = 0, i = 0, fail = 0;

  // How many entries in the array?
  while (child)
    numentries++, child = child->next;
  // Allocate an array to hold the values for each
  entries = (char**) malloc(numentries * sizeof(char*));
  if (!entries)
    return 0;
  memset(entries, 0, numentries * sizeof(char*));
  // Retrieve all the results:
  child = item->child;
  while (child && !fail)
    {
      ret = printValue(child);
      entries[i++] = ret;
      if (ret)
        len += strlen(ret) + 2;
      else
        fail = 1;
      child = child->next;
    }

  // If we didn't fail, try to malloc the output string
  if (!fail)
    out = (char*) malloc(len);
  // If that fails, we fail.
  if (!out)
    fail = 1;

  // Handle failure.
  if (fail)
    {
      for (i = 0; i < numentries; i++)
        if (entries[i])
          free(entries[i]);
      free(entries);
      return 0;
    }

  // Compose the output array.
  *out = '[';
  ptr = out + 1;
  *ptr = 0;
  for (i = 0; i < numentries; i++)
    {
      strcpy(ptr, entries[i]);
      ptr += strlen(entries[i]);
      if (i != numentries - 1)
        {
          *ptr++ = ',';
          *ptr = 0;
        }
      free(entries[i]);
    }
  free(entries);
  *ptr++ = ']';
  *ptr++ = 0;
  return out;
}

// Build an object from the text.
int
aJsonClass::parseObject(aJsonObject *item, FILE* stream)
{
  int in = fgetc(stream);
  if (in != '{')
    return EOF; // not an object!

  item->type = aJson_Object;
  skip(stream);
  in = fgetc(stream);
  if (in == '}')
    {
      return 0; // empty array.
    }

  aJsonObject *child = newItem();
  item->child = child;
  if (item->child == NULL)
    {
      return EOF; //memory fail
    }
  skip(stream);
  if (parseString(child, stream) == EOF)
    {
      return EOF;
    }
  skip(stream);
  child->name = child->value.valuestring;
  child->value.valuestring = NULL;
  in = fgetc(stream);
  if (in != ':')
    {
      return EOF; // fail!
    }
  // skip any spacing, get the value.
  skip(stream);
  if (parseValue(child, stream))
    {
      return EOF;
    }
  skip(stream);
  in = fgetc(stream);
  while (in == ',')
    {
      in = fgetc(stream);
      aJsonObject *new_item = newItem();
      if (new_item == NULL)
        {
          return EOF; // memory fail
        }
      child->next = new_item;
      new_item->prev = child;
      child = new_item;
      skip(stream);
      if (parseString(child, stream) == EOF)
        {
          return EOF;
        }
      skip(stream);
      child->name = child->value.valuestring;
      child->value.valuestring = NULL;

      in = fgetc(stream);
      if (in != ':')
        {
          return EOF; // fail!
        }
      skip(stream);
      if (parseValue(child, stream) == EOF)
        ; // skip any spacing, get the value.
        {
          return EOF;
        }
    }

  in = fgetc(stream);
  if (in == '}')
    {
      return 0; // end of array
    }
  else
    {
      return EOF; // malformed.
    }
}

// Render an object to text.
int
aJsonClass::printObject(aJsonObject *item, FILE* stream)
{
  if (item == NULL)
    {
      //nothing to do
      return 0;
    }
  aJsonObject *child = item->child;
  if (fputc('{', stream) == EOF)
    {
      return EOF;
    }
  while (child)
    {
      if (printValue(child) == EOF)
        {
          return EOF;
        }
      child = child->next;
      if (child)
        {
          if (fputc(',', stream) == EOF)
            {
              return EOF;
            }
        }
    }
  if (fputc('}', stream) == EOF)
    {
      return EOF;
    }
  return 0;
}

// Get Array size/item / object item.
unsigned char
aJsonClass::getArraySize(aJsonObject *array)
{
  aJsonObject *c = array->child;
  unsigned char i = 0;
  while (c)
    i++, c = c->next;
  return i;
}
aJsonObject*
aJsonClass::getArrayItem(aJsonObject *array, unsigned char item)
{
  aJsonObject *c = array->child;
  while (c && item > 0)
    item--, c = c->next;
  return c;
}
aJsonObject*
aJsonClass::getObjectItem(aJsonObject *object, const char *string)
{
  aJsonObject *c = object->child;
  while (c && strcmp(c->name, string))
    c = c->next;
  return c;
}

// Utility for array list handling.
void
aJsonClass::suffixObject(aJsonObject *prev, aJsonObject *item)
{
  prev->next = item;
  item->prev = prev;
}
// Utility for handling references.
aJsonObject*
aJsonClass::createReference(aJsonObject *item)
{
  aJsonObject *ref = newItem();
  if (!ref)
    return 0;
  memcpy(ref, item, sizeof(aJsonObject));
  ref->name = 0;
  ref->type |= aJson_IsReference;
  ref->next = ref->prev = 0;
  return ref;
}

// Add item to array/object.
void
aJsonClass::addItemToArray(aJsonObject *array, aJsonObject *item)
{
  aJsonObject *c = array->child;
  if (!item)
    return;
  if (!c)
    {
      array->child = item;
    }
  else
    {
      while (c && c->next)
        c = c->next;
      suffixObject(c, item);
    }
}
void
aJsonClass::addItemToObject(aJsonObject *object, const char *string,
    aJsonObject *item)
{
  if (!item)
    return;
  if (item->name)
    free(item->name);
  item->name = strdup(string);
  addItemToArray(object, item);
}
void
aJsonClass::addItemReferenceToArray(aJsonObject *array, aJsonObject *item)
{
  addItemToArray(array, createReference(item));
}
void
aJsonClass::addItemReferenceToObject(aJsonObject *object, const char *string,
    aJsonObject *item)
{
  addItemToObject(object, string, createReference(item));
}

aJsonObject*
aJsonClass::detachItemFromArray(aJsonObject *array, unsigned char which)
{
  aJsonObject *c = array->child;
  while (c && which > 0)
    c = c->next, which--;
  if (!c)
    return 0;
  if (c->prev)
    c->prev->next = c->next;
  if (c->next)
    c->next->prev = c->prev;
  if (c == array->child)
    array->child = c->next;
  c->prev = c->next = 0;
  return c;
}
void
aJsonClass::deleteItemFromArray(aJsonObject *array, unsigned char which)
{
  deleteItem(detachItemFromArray(array, which));
}
aJsonObject*
aJsonClass::detachItemFromObject(aJsonObject *object, const char *string)
{
  unsigned char i = 0;
  aJsonObject *c = object->child;
  while (c && strcmp(c->name, string))
    i++, c = c->next;
  if (c)
    return detachItemFromArray(object, i);
  return 0;
}
void
aJsonClass::deleteItemFromObject(aJsonObject *object, const char *string)
{
  deleteItem(detachItemFromObject(object, string));
}

// Replace array/object items with new ones.
void
aJsonClass::replaceItemInArray(aJsonObject *array, unsigned char which,
    aJsonObject *newitem)
{
  aJsonObject *c = array->child;
  while (c && which > 0)
    c = c->next, which--;
  if (!c)
    return;
  newitem->next = c->next;
  newitem->prev = c->prev;
  if (newitem->next)
    newitem->next->prev = newitem;
  if (c == array->child)
    array->child = newitem;
  else
    newitem->prev->next = newitem;
  c->next = c->prev = 0;
  deleteItem(c);
}
void
aJsonClass::replaceItemInObject(aJsonObject *object, const char *string,
    aJsonObject *newitem)
{
  unsigned char i = 0;
  aJsonObject *c = object->child;
  while (c && strcmp(c->name, string))
    i++, c = c->next;
  if (c)
    {
      newitem->name = strdup(string);
      replaceItemInArray(object, i, newitem);
    }
}

// Create basic types:
aJsonObject*
aJsonClass::createNull()
{
  aJsonObject *item = newItem();
  if (item)
    item->type = aJson_NULL;
  return item;
}

aJsonObject*
aJsonClass::createTrue()
{
  aJsonObject *item = newItem();
  if (item)
    {
      item->type = aJson_True;
      item->value.valuebool = -1;
    }
  return item;
}
aJsonObject*
aJsonClass::createFalse()
{
  aJsonObject *item = newItem();
  if (item)
    {
      item->type = aJson_False;
      item->value.valuebool = 0;
    }
  return item;
}
aJsonObject*
aJsonClass::createItem(char b)
{
  aJsonObject *item = newItem();
  if (item)
    {
      item->type = b ? aJson_True : aJson_False;
      item->value.valuebool = b ? -1 : 0;
    }
  return item;
}

aJsonObject*
aJsonClass::createItem(int num)
{
  aJsonObject *item = newItem();
  if (item)
    {
      item->type = aJson_Int;
      item->value.valueint = (int) num;
    }
  return item;
}

aJsonObject*
aJsonClass::createItem(float num)
{
  aJsonObject *item = newItem();
  if (item)
    {
      item->type = aJson_Float;
      item->value.valuefloat = num;
    }
  return item;
}

aJsonObject*
aJsonClass::createItem(const char *string)
{
  aJsonObject *item = newItem();
  if (item)
    {
      item->type = aJson_String;
      item->value.valuestring = strdup(string);
    }
  return item;
}

aJsonObject*
aJsonClass::createArray()
{
  aJsonObject *item = newItem();
  if (item)
    item->type = aJson_Array;
  return item;
}
aJsonObject*
aJsonClass::createObject()
{
  aJsonObject *item = newItem();
  if (item)
    item->type = aJson_Object;
  return item;
}

// Create Arrays:
aJsonObject*
aJsonClass::createIntArray(int *numbers, unsigned char count)
{
  unsigned char i;
  aJsonObject *n = 0, *p = 0, *a = createArray();
  for (i = 0; a && i < count; i++)
    {
      n = createItem(numbers[i]);
      if (!i)
        a->child = n;
      else
        suffixObject(p, n);
      p = n;
    }
  return a;
}

aJsonObject*
aJsonClass::createFloatArray(float *numbers, unsigned char count)
{
  unsigned char i;
  aJsonObject *n = 0, *p = 0, *a = createArray();
  for (i = 0; a && i < count; i++)
    {
      n = createItem(numbers[i]);
      if (!i)
        a->child = n;
      else
        suffixObject(p, n);
      p = n;
    }
  return a;
}

aJsonObject*
aJsonClass::createDoubleArray(float *numbers, unsigned char count)
{
  unsigned char i;
  aJsonObject *n = 0, *p = 0, *a = createArray();
  for (i = 0; a && i < count; i++)
    {
      n = createItem(numbers[i]);
      if (!i)
        a->child = n;
      else
        suffixObject(p, n);
      p = n;
    }
  return a;
}

aJsonObject*
aJsonClass::createStringArray(const char **strings, unsigned char count)
{
  unsigned char i;
  aJsonObject *n = 0, *p = 0, *a = createArray();
  for (i = 0; a && i < count; i++)
    {
      n = createItem(strings[i]);
      if (!i)
        a->child = n;
      else
        suffixObject(p, n);
      p = n;
    }
  return a;
}

void
aJsonClass::addNullToObject(aJsonObject* object, const char* name)
{
  addItemToObject(object, name, createNull());
}

void
aJsonClass::addTrueToObject(aJsonObject* object, const char* name)
{
  addItemToObject(object, name, createTrue());
}

void
aJsonClass::addFalseToObject(aJsonObject* object, const char* name)
{
  addItemToObject(object, name, createFalse());
}

void
aJsonClass::addNumberToObject(aJsonObject* object, const char* name, int n)
{
  addItemToObject(object, name, createItem(n));
}

void
aJsonClass::addStringToObject(aJsonObject* object, const char* name,
    const char* s)
{
  addItemToObject(object, name, createItem(s));
}

char*
aJsonClass::addToBuffer(char value, char* buffer, unsigned int* buffer_length,
    unsigned int* buffer_bytes)
{
  if ((buffer_bytes + 1) >= buffer_length)
    {
      buffer = (char*) realloc((void*) buffer, (*buffer_length
          + BUFFER_DEFAULT_SIZE) * sizeof(char));
      if (buffer == NULL)
        {
          return NULL;
        }
      *buffer_length += BUFFER_DEFAULT_SIZE;
      buffer[*buffer_bytes] = value;
      *buffer_bytes += 1;
      return buffer;
    }
  return buffer;
}

//TODO conversion routines btw. float & int types?

aJsonClass aJson;
