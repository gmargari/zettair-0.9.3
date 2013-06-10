/* ascii.h includes some declarations that allow categorisation of
 * ASCII values in case statements, which i find very useful in
 * constructing parsers.
 *
 * XXX: the symbolic forms are nice, but pure numbers would allow
 * correct operation on non-ASCII machines
 *
 * written nml 2004-04-30
 *
 */

#ifndef ASCII_H
#define ASCII_H

#ifdef __cplusplus
extern "C" {
#endif

/* catch uppercase letters in a case statement */
#define ASCII_CASE_UPPER                                                      \
         'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G':     \
    case 'H': case 'I': case 'J': case 'K': case 'L': case 'M': case 'N':     \
    case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U':     \
    case 'V': case 'W': case 'X': case 'Y': case 'Z'                         

/* catch lowercase letters in a case statement */
#define ASCII_CASE_LOWER                                                      \
         'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g':     \
    case 'h': case 'i': case 'j': case 'k': case 'l': case 'm': case 'n':     \
    case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u':     \
    case 'v': case 'w': case 'x': case 'y': case 'z'

/* catch extended ASCII charset (> 127) for preservation as per UTF8 */
#define ASCII_CASE_EXTENDED                                                   \
         (char) 128: case (char) 129: case (char) 130: case (char) 131:       \
    case (char) 132: case (char) 133: case (char) 134: case (char) 135:       \
    case (char) 136: case (char) 137: case (char) 138: case (char) 139:       \
    case (char) 140: case (char) 141: case (char) 142: case (char) 143:       \
    case (char) 144: case (char) 145: case (char) 146: case (char) 147:       \
    case (char) 148: case (char) 149: case (char) 150: case (char) 151:       \
    case (char) 152: case (char) 153: case (char) 154: case (char) 155:       \
    case (char) 156: case (char) 157: case (char) 158: case (char) 159:       \
    case (char) 160: case (char) 161: case (char) 162: case (char) 163:       \
    case (char) 164: case (char) 165: case (char) 166: case (char) 167:       \
    case (char) 168: case (char) 169: case (char) 170: case (char) 171:       \
    case (char) 172: case (char) 173: case (char) 174: case (char) 175:       \
    case (char) 176: case (char) 177: case (char) 178: case (char) 179:       \
    case (char) 180: case (char) 181: case (char) 182: case (char) 183:       \
    case (char) 184: case (char) 185: case (char) 186: case (char) 187:       \
    case (char) 188: case (char) 189: case (char) 190: case (char) 191:       \
    case (char) 192: case (char) 193: case (char) 194: case (char) 195:       \
    case (char) 196: case (char) 197: case (char) 198: case (char) 199:       \
    case (char) 200: case (char) 201: case (char) 202: case (char) 203:       \
    case (char) 204: case (char) 205: case (char) 206: case (char) 207:       \
    case (char) 208: case (char) 209: case (char) 210: case (char) 211:       \
    case (char) 212: case (char) 213: case (char) 214: case (char) 215:       \
    case (char) 216: case (char) 217: case (char) 218: case (char) 219:       \
    case (char) 220: case (char) 221: case (char) 222: case (char) 223:       \
    case (char) 224: case (char) 225: case (char) 226: case (char) 227:       \
    case (char) 228: case (char) 229: case (char) 230: case (char) 231:       \
    case (char) 232: case (char) 233: case (char) 234: case (char) 235:       \
    case (char) 236: case (char) 237: case (char) 238: case (char) 239:       \
    case (char) 240: case (char) 241: case (char) 242: case (char) 243:       \
    case (char) 244: case (char) 245: case (char) 246: case (char) 247:       \
    case (char) 248: case (char) 249: case (char) 250: case (char) 251:       \
    case (char) 252: case (char) 253: case (char) 254: case (char) 255 

/* catch control characters (all non-printing non-whitespace ASCII 
 * characters) */
#define ASCII_CASE_CONTROL                                                    \
         127: case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: \
    case 14: case 15: case 16: case 17: case 18: case 19: case 20: case 21:   \
    case 22: case 23: case 24: case 25: case 26: case 27: case 28: case 29:   \
    case 30: case 31

/* catch all ASCII punctuation characters */
#define ASCII_CASE_PUNCTUATION                                                \
         '!': case '"': case '#': case '$': case '%': case '&': case '\'':    \
    case '(': case ')': case '*': case '+': case ',': case '-': case '.':     \
    case '/': case ':': case ';': case '<': case '=': case '>': case '?':     \
    case '@': case '[': case '\\': case ']': case '^': case '_': case '`':    \
    case '{': case '|': case '}': case '~'

/* catch digits in a case statement */
#define ASCII_CASE_DIGIT                                                      \
         '0': case '1': case '2': case '3': case '4': case '5': case '6':     \
    case '7': case '8': case '9'

/* catch whitespace characters in a case statement (no ending colon and 
 * no first case so we can use case syntax) note: for our purposes, \0 is a 
 * space character. */
#define ASCII_CASE_SPACE                                                      \
         ' ': case '\t': case '\n': case '\f': case '\v': case '\r': case '\0'

/* ASCII optimised versions of toupper and tolower (though they only work on 
 * upper and lowercase letters respectively) */
#define ASCII_TOLOWER(c) (c | 0x20)   
#define ASCII_TOUPPER(c) (c & ~0x20)

#ifdef __cplusplus
}
#endif

#endif

