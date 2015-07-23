// Autogenerated by metajava.py.
// Do not edit this file directly.
// The template for this file is located at:
// ../../../../../../../../templates/AstSubclass.java
package com.rethinkdb.ast.gen;

import com.rethinkdb.ast.helper.Arguments;
import com.rethinkdb.ast.helper.OptArgs;
import com.rethinkdb.ast.RqlAst;
import com.rethinkdb.proto.TermType;
import java.util.*;



public class ToIso8601 extends RqlQuery {


    public ToIso8601(java.lang.Object arg) {
        this(new Arguments(arg), null);
    }
    public ToIso8601(Arguments args, OptArgs optargs) {
        this(null, args, optargs);
    }
    public ToIso8601(RqlAst prev, Arguments args, OptArgs optargs) {
        this(prev, TermType.TO_ISO8601, args, optargs);
    }
    protected ToIso8601(RqlAst previous, TermType termType, Arguments args, OptArgs optargs){
        super(previous, termType, args, optargs);
    }


    /* Static factories */
    public static ToIso8601 fromArgs(Object... args){
        return new ToIso8601(new Arguments(args), null);
    }


}