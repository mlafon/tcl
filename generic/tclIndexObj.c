/* 
 * tclIndexObj.c --
 *
 *	This file implements objects of type "index".  This object type
 *	is used to lookup a keyword in a table of valid values and cache
 *	the index of the matching entry.
 *
 * Copyright (c) 1997 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: tclIndexObj.c,v 1.22.2.1 2005/07/12 20:36:53 kennykb Exp $
 */

#include "tclInt.h"

/*
 * Prototypes for procedures defined later in this file:
 */

static int		SetIndexFromAny _ANSI_ARGS_((Tcl_Interp *interp,
			    Tcl_Obj *objPtr));
static void		UpdateStringOfIndex _ANSI_ARGS_((Tcl_Obj *objPtr));
static void		DupIndex _ANSI_ARGS_((Tcl_Obj *srcPtr,
			    Tcl_Obj *dupPtr));
static void		FreeIndex _ANSI_ARGS_((Tcl_Obj *objPtr));

/*
 * The structure below defines the index Tcl object type by means of
 * procedures that can be invoked by generic object code.
 */

static Tcl_ObjType indexType = {
    "index",				/* name */
    FreeIndex,				/* freeIntRepProc */
    DupIndex,				/* dupIntRepProc */
    UpdateStringOfIndex,		/* updateStringProc */
    SetIndexFromAny			/* setFromAnyProc */
};

/*
 * The definition of the internal representation of the "index"
 * object; The internalRep.otherValuePtr field of an object of "index"
 * type will be a pointer to one of these structures.
 *
 * Keep this structure declaration in sync with tclTestObj.c
 */

typedef struct {
    VOID *tablePtr;			/* Pointer to the table of strings */
    int offset;				/* Offset between table entries */
    int index;				/* Selected index into table. */
} IndexRep;

/*
 * The following macros greatly simplify moving through a table...
 */
#define STRING_AT(table, offset, index) \
	(*((CONST char * CONST *)(((char *)(table)) + ((offset) * (index)))))
#define NEXT_ENTRY(table, offset) \
	(&(STRING_AT(table, offset, 1)))
#define EXPAND_OF(indexRep) \
	STRING_AT((indexRep)->tablePtr, (indexRep)->offset, (indexRep)->index)


/*
 *----------------------------------------------------------------------
 *
 * Tcl_GetIndexFromObj --
 *
 *	This procedure looks up an object's value in a table of strings
 *	and returns the index of the matching string, if any.
 *
 * Results:
 *
 *	If the value of objPtr is identical to or a unique abbreviation
 *	for one of the entries in objPtr, then the return value is
 *	TCL_OK and the index of the matching entry is stored at
 *	*indexPtr.  If there isn't a proper match, then TCL_ERROR is
 *	returned and an error message is left in interp's result (unless
 *	interp is NULL).  The msg argument is used in the error
 *	message; for example, if msg has the value "option" then the
 *	error message will say something flag 'bad option "foo": must be
 *	...'
 *
 * Side effects:
 *	The result of the lookup is cached as the internal rep of
 *	objPtr, so that repeated lookups can be done quickly.
 *
 *----------------------------------------------------------------------
 */

int
Tcl_GetIndexFromObj(interp, objPtr, tablePtr, msg, flags, indexPtr)
    Tcl_Interp *interp; 	/* Used for error reporting if not NULL. */
    Tcl_Obj *objPtr;		/* Object containing the string to lookup. */
    CONST char **tablePtr;	/* Array of strings to compare against the
				 * value of objPtr; last entry must be NULL
				 * and there must not be duplicate entries. */
    CONST char *msg;		/* Identifying word to use in error messages. */
    int flags;			/* 0 or TCL_EXACT */
    int *indexPtr;		/* Place to store resulting integer index. */
{

    /*
     * See if there is a valid cached result from a previous lookup
     * (doing the check here saves the overhead of calling
     * Tcl_GetIndexFromObjStruct in the common case where the result
     * is cached).
     */

    if (objPtr->typePtr == &indexType) {
	IndexRep *indexRep = (IndexRep *) objPtr->internalRep.otherValuePtr;
	/*
	 * Here's hoping we don't get hit by unfortunate packing
	 * constraints on odd platforms like a Cray PVP...
	 */
	if (indexRep->tablePtr == (VOID *)tablePtr &&
		indexRep->offset == sizeof(char *)) {
	    *indexPtr = indexRep->index;
	    return TCL_OK;
	}
    }
    return Tcl_GetIndexFromObjStruct(interp, objPtr, tablePtr, sizeof(char *),
	    msg, flags, indexPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_GetIndexFromObjStruct --
 *
 *	This procedure looks up an object's value given a starting
 *	string and an offset for the amount of space between strings.
 *	This is useful when the strings are embedded in some other
 *	kind of array.
 *
 * Results:
 *
 *	If the value of objPtr is identical to or a unique abbreviation
 *	for one of the entries in objPtr, then the return value is
 *	TCL_OK and the index of the matching entry is stored at
 *	*indexPtr.  If there isn't a proper match, then TCL_ERROR is
 *	returned and an error message is left in interp's result (unless
 *	interp is NULL).  The msg argument is used in the error
 *	message; for example, if msg has the value "option" then the
 *	error message will say something flag 'bad option "foo": must be
 *	...'
 *
 * Side effects:
 *	The result of the lookup is cached as the internal rep of
 *	objPtr, so that repeated lookups can be done quickly.
 *
 *----------------------------------------------------------------------
 */

int
Tcl_GetIndexFromObjStruct(interp, objPtr, tablePtr, offset, msg, flags, 
	indexPtr)
    Tcl_Interp *interp; 	/* Used for error reporting if not NULL. */
    Tcl_Obj *objPtr;		/* Object containing the string to lookup. */
    CONST VOID *tablePtr;	/* The first string in the table. The second
				 * string will be at this address plus the
				 * offset, the third plus the offset again,
				 * etc. The last entry must be NULL
				 * and there must not be duplicate entries. */
    int offset;			/* The number of bytes between entries */
    CONST char *msg;		/* Identifying word to use in error messages. */
    int flags;			/* 0 or TCL_EXACT */
    int *indexPtr;		/* Place to store resulting integer index. */
{
    int index, length, i, numAbbrev;
    char *key, *p1;
    CONST char *p2;
    CONST char * CONST *entryPtr;
    Tcl_Obj *resultPtr;
    IndexRep *indexRep;

    /*
     * See if there is a valid cached result from a previous lookup.
     */

    if (objPtr->typePtr == &indexType) {
	indexRep = (IndexRep *) objPtr->internalRep.otherValuePtr;
	if (indexRep->tablePtr==tablePtr && indexRep->offset==offset) {
	    *indexPtr = indexRep->index;
	    return TCL_OK;
	}
    }

    /*
     * Lookup the value of the object in the table.  Accept unique
     * abbreviations unless TCL_EXACT is set in flags.
     */

    key = Tcl_GetStringFromObj(objPtr, &length);
    index = -1;
    numAbbrev = 0;

    /*
     * The key should not be empty, otherwise it's not a match.
     */
    
    if (key[0] == '\0') {
	goto error;
    }
    
    /*
     * Scan the table looking for one of:
     *  - An exact match (always preferred)
     *  - A single abbreviation (allowed depending on flags)
     *  - Several abbreviations (never allowed, but overridden by exact match)
     */
    for (entryPtr = tablePtr, i = 0; *entryPtr != NULL; 
	    entryPtr = NEXT_ENTRY(entryPtr, offset), i++) {
	for (p1 = key, p2 = *entryPtr; *p1 == *p2; p1++, p2++) {
	    if (*p1 == '\0') {
		index = i;
		goto done;
	    }
	}
	if (*p1 == '\0') {
	    /*
	     * The value is an abbreviation for this entry.  Continue
	     * checking other entries to make sure it's unique.  If we
	     * get more than one unique abbreviation, keep searching to
	     * see if there is an exact match, but remember the number
	     * of unique abbreviations and don't allow either.
	     */

	    numAbbrev++;
	    index = i;
	}
    }
    /*
     * Check if we were instructed to disallow abbreviations.
     */
    if ((flags & TCL_EXACT) || (numAbbrev != 1)) {
	goto error;
    }

    done:
    /*
     * Cache the found representation.  Note that we want to avoid
     * allocating a new internal-rep if at all possible since that is
     * potentially a slow operation.
     */
    if (objPtr->typePtr == &indexType) {
 	indexRep = (IndexRep *) objPtr->internalRep.otherValuePtr;
    } else {
	TclFreeIntRep(objPtr);
 	indexRep = (IndexRep *) ckalloc(sizeof(IndexRep));
 	objPtr->internalRep.otherValuePtr = (VOID *) indexRep;
 	objPtr->typePtr = &indexType;
    }
    indexRep->tablePtr = (VOID*) tablePtr;
    indexRep->offset = offset;
    indexRep->index = index;

    *indexPtr = index;
    return TCL_OK;

    error:
    if (interp != NULL) {
	/*
	 * Produce a fancy error message.
	 */
	int count;

	TclNewObj(resultPtr);
	Tcl_SetObjResult(interp, resultPtr);
	Tcl_AppendStringsToObj(resultPtr,
		(numAbbrev > 1) ? "ambiguous " : "bad ", msg, " \"",
		key, "\": must be ", STRING_AT(tablePtr,offset,0), (char*)NULL);
	for (entryPtr = NEXT_ENTRY(tablePtr, offset), count = 0;
		*entryPtr != NULL;
		entryPtr = NEXT_ENTRY(entryPtr, offset), count++) {
	    if (*NEXT_ENTRY(entryPtr, offset) == NULL) {
		Tcl_AppendStringsToObj(resultPtr,
			(count > 0) ? ", or " : " or ", *entryPtr,
			(char *) NULL);
	    } else {
		Tcl_AppendStringsToObj(resultPtr, ", ", *entryPtr,
			(char *) NULL);
	    }
	}
    }
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * SetIndexFromAny --
 *
 *	This procedure is called to convert a Tcl object to index
 *	internal form. However, this doesn't make sense (need to have a
 *	table of keywords in order to do the conversion) so the
 *	procedure always generates an error.
 *
 * Results:
 *	The return value is always TCL_ERROR, and an error message is
 *	left in interp's result if interp isn't NULL. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
SetIndexFromAny(interp, objPtr)
    Tcl_Interp *interp;		/* Used for error reporting if not NULL. */
    register Tcl_Obj *objPtr;	/* The object to convert. */
{
    Tcl_SetObjResult(interp, Tcl_NewStringObj(
	    "can't convert value to index except via Tcl_GetIndexFromObj API",
	    -1));
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateStringOfIndex --
 *
 *	This procedure is called to convert a Tcl object from index
 *	internal form to its string form.  No abbreviation is ever
 *	generated.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The string representation of the object is updated.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateStringOfIndex(objPtr)
    Tcl_Obj *objPtr;
{
    IndexRep *indexRep = (IndexRep *) objPtr->internalRep.otherValuePtr;
    register char *buf;
    register unsigned len;
    register CONST char *indexStr = EXPAND_OF(indexRep);

    len = strlen(indexStr);
    buf = (char *) ckalloc(len + 1);
    memcpy(buf, indexStr, len+1);
    objPtr->bytes = buf;
    objPtr->length = len;
}

/*
 *----------------------------------------------------------------------
 *
 * DupIndex --
 *
 *	This procedure is called to copy the internal rep of an index
 *	Tcl object from to another object.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The internal representation of the target object is updated
 *	and the type is set.
 *
 *----------------------------------------------------------------------
 */

static void
DupIndex(srcPtr, dupPtr)
    Tcl_Obj *srcPtr, *dupPtr;
{
    IndexRep *srcIndexRep = (IndexRep *) srcPtr->internalRep.otherValuePtr;
    IndexRep *dupIndexRep = (IndexRep *) ckalloc(sizeof(IndexRep));

    memcpy(dupIndexRep, srcIndexRep, sizeof(IndexRep));
    dupPtr->internalRep.otherValuePtr = (VOID *) dupIndexRep;
    dupPtr->typePtr = &indexType;
}

/*
 *----------------------------------------------------------------------
 *
 * FreeIndex --
 *
 *	This procedure is called to delete the internal rep of an index
 *	Tcl object.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The internal representation of the target object is deleted.
 *
 *----------------------------------------------------------------------
 */

static void
FreeIndex(objPtr)
    Tcl_Obj *objPtr;
{
    ckfree((char *) objPtr->internalRep.otherValuePtr);
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_WrongNumArgs --
 *
 *	This procedure generates a "wrong # args" error message in an
 *	interpreter.  It is used as a utility function by many command
 *	procedures.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	An error message is generated in interp's result object to
 *	indicate that a command was invoked with the wrong number of
 *	arguments.  The message has the form
 *		wrong # args: should be "foo bar additional stuff"
 *	where "foo" and "bar" are the initial objects in objv (objc
 *	determines how many of these are printed) and "additional stuff"
 *	is the contents of the message argument.
 *
 *----------------------------------------------------------------------
 */

void
Tcl_WrongNumArgs(interp, objc, objv, message)
    Tcl_Interp *interp;			/* Current interpreter. */
    int objc;				/* Number of arguments to print
					 * from objv. */
    Tcl_Obj *CONST objv[];		/* Initial argument objects, which
					 * should be included in the error
					 * message. */
    CONST char *message;		/* Error message to print after the
					 * leading objects in objv. The
					 * message may be NULL. */
{
    Tcl_Obj *objPtr;
    int i, len, elemLen, flags;
    register IndexRep *indexRep;
    Interp *iPtr = (Interp *) interp;
    char *elementStr;
#ifndef AVOID_HACKS_FOR_ITCL
    int isFirst = 1;			/* Special flag used to inhibit the
					 * treating of the first word as a
					 * list element so the hacky way Itcl
					 * does error message generation for
					 * ensembles will still work.
					 * [Bug 1066837] */
#define MAY_QUOTE_WORD (!isFirst)
#else /* !AVOID_HACKS_FOR_ITCL */
#define MAY_QUOTE_WORD 1
#endif /* AVOID_HACKS_FOR_ITCL */

    TclNewObj(objPtr);
    if (iPtr->flags & INTERP_ALTERNATE_WRONG_ARGS) {
	Tcl_AppendObjToObj(objPtr, Tcl_GetObjResult(interp));
	Tcl_AppendToObj(objPtr, " or \"", -1);
    } else {
	Tcl_AppendToObj(objPtr, "wrong # args: should be \"", -1);
    }

    /*
     * Check to see if we are processing an ensemble implementation,
     * and if so rewrite the results in terms of how the ensemble was
     * invoked.
     */

    if (iPtr->ensembleRewrite.sourceObjs != NULL) {
	/*
	 * We only know how to do rewriting if all the replaced
	 * objects are actually arguments (in objv) to this function.
	 * Otherwise it just gets too complicated...
	 */

	if (objc >= iPtr->ensembleRewrite.numInsertedObjs) {
	    objv += iPtr->ensembleRewrite.numInsertedObjs;
	    objc -= iPtr->ensembleRewrite.numInsertedObjs;
	    /*
	     * We assume no object is of index type.
	     */
	    for (i=0 ; i<iPtr->ensembleRewrite.numRemovedObjs ; i++) {
		/*
		 * Add the element, quoting it if necessary.
		 */

		elementStr = Tcl_GetStringFromObj(
			iPtr->ensembleRewrite.sourceObjs[i], &elemLen);
		len = Tcl_ScanCountedElement(elementStr, elemLen, &flags);
		if (MAY_QUOTE_WORD && len != elemLen) {
		    char *quotedElementStr = ckalloc((unsigned) len);
		    len = Tcl_ConvertCountedElement(elementStr, elemLen,
			    quotedElementStr, flags);
		    Tcl_AppendToObj(objPtr, quotedElementStr, len);
		    ckfree(quotedElementStr);
		} else {
		    Tcl_AppendToObj(objPtr, elementStr, elemLen);
		}
#ifndef AVOID_HACKS_FOR_ITCL
		isFirst = 0;
#endif /* AVOID_HACKS_FOR_ITCL */

		/*
		 * Add a space if the word is not the last one (which
		 * has a moderately complex condition here).
		 */

		if ((i < (iPtr->ensembleRewrite.numRemovedObjs - 1))
			|| objc || message) {
		    Tcl_AppendStringsToObj(objPtr, " ", (char *) NULL);
		}
	    }
	}
    }

    /*
     * Now add the arguments (other than those rewritten) that the
     * caller took from its calling context.
     */

    for (i = 0; i < objc; i++) {
	/*
	 * If the object is an index type use the index table which allows
	 * for the correct error message even if the subcommand was
	 * abbreviated.  Otherwise, just use the string rep.
	 */
	
	if (objv[i]->typePtr == &indexType) {
	    indexRep = (IndexRep *) objv[i]->internalRep.otherValuePtr;
	    Tcl_AppendStringsToObj(objPtr, EXPAND_OF(indexRep), (char *) NULL);
	} else {
	    /*
	     * Quote the argument if it contains spaces (Bug 942757).
	     */

	    elementStr = Tcl_GetStringFromObj(objv[i], &elemLen);
	    len = Tcl_ScanCountedElement(elementStr, elemLen, &flags);
	    if (MAY_QUOTE_WORD && len != elemLen) {
		char *quotedElementStr = ckalloc((unsigned) len);
		len = Tcl_ConvertCountedElement(elementStr, elemLen,
			quotedElementStr, flags);
		Tcl_AppendToObj(objPtr, quotedElementStr, len);
		ckfree(quotedElementStr);
	    } else {
		Tcl_AppendToObj(objPtr, elementStr, elemLen);
	    }
	}
#ifndef AVOID_HACKS_FOR_ITCL
	isFirst = 0;
#endif /* AVOID_HACKS_FOR_ITCL */

	/*
	 * Append a space character (" ") if there is more text to follow
	 * (either another element from objv, or the message string).
	 */
	if ((i < (objc - 1)) || message) {
	    Tcl_AppendStringsToObj(objPtr, " ", (char *) NULL);
	}
    }

    /*
     * Add any trailing message bits and set the resulting string as
     * the interpreter result. Caller is responsible for reporting
     * this as an actual error.
     */

    if (message) {
	Tcl_AppendStringsToObj(objPtr, message, (char *) NULL);
    }
    Tcl_AppendStringsToObj(objPtr, "\"", (char *) NULL);
    Tcl_SetObjResult(interp, objPtr);
#undef MAY_QUOTE_WORD
}
