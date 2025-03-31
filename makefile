# Updated Makefile with interpreter support
OCAMLC=ocamlc
OCAMLLEX=ocamllex
OCAMLYACC=ocamlyacc

DSL_PARSER=mydsl

all: $(DSL_PARSER)

# Add interpreter.cmo to dependencies
$(DSL_PARSER): ast.cmo parser.cmo lexer.cmo type_checker.cmo interpreter.cmo main.cmo
	$(OCAMLC) -o $@ $^

# Add interpreter build rule
interpreter.cmo: interpreter.ml ast.cmo type_checker.cmo
	$(OCAMLC) -c $<

# Rest of the original rules remain the same
ast.cmo: ast.ml
	$(OCAMLC) -c $<

parser.ml parser.mli: parser.mly
	$(OCAMLYACC) $<

parser.cmi: parser.mli ast.cmo
	$(OCAMLC) -c $<

parser.cmo: parser.ml parser.cmi
	$(OCAMLC) -c $<

lexer.ml: lexer.mll
	$(OCAMLLEX) $<

lexer.cmo: lexer.ml parser.cmi
	$(OCAMLC) -c $<

type_checker.cmo: type_checker.ml ast.cmo
	$(OCAMLC) -c $<

main.cmo: main.ml ast.cmo parser.cmi lexer.cmo type_checker.cmo interpreter.cmo
	$(OCAMLC) -c $<

clean:
	rm -f *.cmo *.cmi lexer.ml parser.ml parser.mli $(DSL_PARSER)

.PHONY: all clean

run:
	./mydsl < ./inputs/t.txt
