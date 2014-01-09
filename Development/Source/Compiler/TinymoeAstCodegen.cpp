#include "TinymoeAstCodegen.h"

using namespace tinymoe::ast;

namespace tinymoe
{
	namespace compiler
	{
		class SymbolAstScope
		{
		public:
			typedef shared_ptr<SymbolAstScope>							Ptr;
			typedef map<GrammarSymbol::Ptr, AstDeclaration::Ptr>		SymbolAstDeclarationMap;

			SymbolAstDeclarationMap					readAsts;
			SymbolAstDeclarationMap					writeAsts;

			AstType::Ptr GetType(GrammarSymbol::Ptr symbol, AstNode::Ptr parent)
			{
				if (symbol->target == GrammarSymbolTarget::Custom)
				{
					auto type = make_shared<AstReferenceType>();
					type->parent = parent;
					type->typeDeclaration = dynamic_pointer_cast<AstTypeDeclaration>(readAsts.find(symbol)->second);
					return type;
				}
				else
				{
					AstPredefinedTypeName typeName = AstPredefinedTypeName::Object;
					switch (symbol->target)
					{
					case GrammarSymbolTarget::Object:
						typeName = AstPredefinedTypeName::Object;
						break;
					case GrammarSymbolTarget::Array:
						typeName = AstPredefinedTypeName::Array;
						break;
					case GrammarSymbolTarget::Symbol:
						typeName = AstPredefinedTypeName::Symbol;
						break;
					case GrammarSymbolTarget::Boolean:
						typeName = AstPredefinedTypeName::Boolean;
						break;
					case GrammarSymbolTarget::Integer:
						typeName = AstPredefinedTypeName::Integer;
						break;
					case GrammarSymbolTarget::Float:
						typeName = AstPredefinedTypeName::Float;
						break;
					case GrammarSymbolTarget::String:
						typeName = AstPredefinedTypeName::String;
						break;
					}

					auto type = make_shared<AstPredefinedType>();
					type->parent = parent;
					type->typeName = typeName;
					return type;
				}
			}
		};

		struct SymbolAstContext
		{
			int										uniqueId = 0;

			string GetUniquePostfix()
			{
				stringstream ss;
				ss << "_" << uniqueId++;
				return ss.str();
			}
		};

		struct SymbolAstResult
		{
			shared_ptr<AstExpression>				value;
			shared_ptr<AstStatement>				statement;
			shared_ptr<AstLambdaExpression>			continuation;

			SymbolAstResult()
			{
			}

			SymbolAstResult(shared_ptr<AstExpression> _value)
				:value(_value)
			{
			}

			SymbolAstResult(shared_ptr<AstExpression> _value, shared_ptr<AstStatement> _statement, shared_ptr<AstLambdaExpression> _continuation)
				:value(_value)
				, statement(_statement)
				, continuation(_continuation)
			{
			}

			bool RequireCps()
			{
				return statement && continuation;
			}

			SymbolAstResult ReplaceValue(shared_ptr<AstExpression> _value)
			{
				return SymbolAstResult(_value, statement, continuation);
			}

			static SymbolAstResult Merge(SymbolAstContext& context, vector<SymbolAstResult>& results)
			{
				SymbolAstResult merged;
				auto start = results.begin();

				for (auto it = results.begin(); it != results.end(); it++)
				{
					if (it->RequireCps())
					{
						auto block = make_shared<AstBlockStatement>();
						for (auto it2 = start; it2 != it; it2++)
						{
							auto var = make_shared<AstDeclarationStatement>();
							{
								auto decl = make_shared<AstSymbolDeclaration>();
								decl->parent = var;
								decl->composedName = "$var" + context.GetUniquePostfix();
								var->declaration = decl;

								auto assign = make_shared<AstAssignmentStatement>();
								assign->parent = block;
								block->statements.push_back(assign);

								auto ref = make_shared<AstReferenceExpression>();
								ref->parent = assign;
								ref->reference = decl;
								assign->target = ref;

								it2->value->parent = assign;
								assign->value = it2->value;
							}
					
							auto ref = make_shared<AstReferenceExpression>();
							ref->reference = var->declaration;
							it2->value = ref;
						}

						if (merged.continuation)
						{
							merged.continuation->statement = block;
						}
						else
						{
							merged.statement = block;
						}
						merged.continuation = it->continuation;
					}
				}
				return merged;
			}
		};

		/*************************************************************
		FunctionFragment::GetComposedName
		*************************************************************/

		string NameFragment::GetComposedName(bool primitive)
		{
			return name->GetComposedName();
		}

		string VariableArgumentFragment::GetComposedName(bool primitive)
		{
			string result;
			switch (type)
			{
			case FunctionArgumentType::Argument:
				result = "$argument";
				break;
			case FunctionArgumentType::Assignable:
				result = "assignable";
				break;
			case FunctionArgumentType::Expression:
				result = (primitive ? "$primitive" : "$expression");
				break;
			case FunctionArgumentType::List:
				result = "list";
				break;
			case FunctionArgumentType::Normal:
				result = (primitive ? "$primitive" : "$expression");
				break;
			}
			
			if (receivingType)
			{
				result += "<" + receivingType->GetComposedName() + ">";
			}
			return result;
		}

		string FunctionArgumentFragment::GetComposedName(bool primitive)
		{
			return (primitive ? "$primitive" : "$expression");
		}

		/*************************************************************
		FunctionFragment::GenerateAst
		*************************************************************/

		FunctionFragment::AstPair NameFragment::CreateAst(weak_ptr<ast::AstNode> parent)
		{
			return AstPair(nullptr, nullptr);
		}

		FunctionFragment::AstPair VariableArgumentFragment::CreateAst(weak_ptr<ast::AstNode> parent)
		{
			if (type == FunctionArgumentType::Argument)
			{
				return AstPair(nullptr, nullptr);
			}
			else if (type == FunctionArgumentType::Assignable)
			{
				auto read = make_shared<AstSymbolDeclaration>();
				read->parent = parent;
				read->composedName = "$read_" + name->GetComposedName();

				auto write = make_shared<AstSymbolDeclaration>();
				write->parent = parent;
				write->composedName = "$write_" + name->GetComposedName();
				return AstPair(read, write);
			}
			else
			{
				auto ast = make_shared<AstSymbolDeclaration>();
				ast->parent = parent;
				ast->composedName = name->GetComposedName();
				return AstPair(ast, nullptr);
			}
		}

		FunctionFragment::AstPair FunctionArgumentFragment::CreateAst(weak_ptr<ast::AstNode> parent)
		{
			auto ast = make_shared<AstSymbolDeclaration>();
			ast->parent = parent;
			ast->composedName = declaration->GetComposedName();
			return AstPair(ast, nullptr);
		}

		/*************************************************************
		Declaration::GenerateAst
		*************************************************************/

		shared_ptr<ast::AstDeclaration> SymbolDeclaration::GenerateAst(shared_ptr<SymbolAstScope> scope, shared_ptr<SymbolModule> symbolModule, weak_ptr<ast::AstNode> parent)
		{
			auto ast = make_shared<AstSymbolDeclaration>();
			ast->parent = parent;
			ast->composedName = symbolModule->module->name->GetComposedName() + "::" + name->GetComposedName();
			return ast;
		}

		shared_ptr<ast::AstDeclaration> TypeDeclaration::GenerateAst(shared_ptr<SymbolAstScope> scope, shared_ptr<SymbolModule> symbolModule, weak_ptr<ast::AstNode> parent)
		{
			auto ast = make_shared<AstTypeDeclaration>();
			ast->parent = parent;
			ast->composedName = symbolModule->module->name->GetComposedName() + "::" + name->GetComposedName();
			for (auto field : fields)
			{
				auto astField = make_shared<AstSymbolDeclaration>();
				astField->parent = ast;
				astField->composedName = field->GetComposedName();
				ast->fields.push_back(astField);
			}
			return ast;
		}

		string FunctionDeclaration::GetComposedName()
		{
			string result;
			for (auto it = name.begin(); it != name.end(); it++)
			{
				result += (*it)->GetComposedName(type == FunctionDeclarationType::Phrase && (it == name.begin() || it + 1 == name.end()));
				if (it + 1 != name.end())
				{
					result += "_";
				}
			}
			return result;
		}

		shared_ptr<ast::AstDeclaration> FunctionDeclaration::GenerateAst(shared_ptr<SymbolAstScope> scope, shared_ptr<SymbolModule> symbolModule, weak_ptr<ast::AstNode> parent)
		{
			auto ast = make_shared<AstFunctionDeclaration>();
			ast->parent = parent;
			ast->composedName = symbolModule->module->name->GetComposedName() + "::" + GetComposedName();
			
			{
				auto argument = make_shared<AstSymbolDeclaration>();
				argument->parent = ast;
				argument->composedName = "$the_result";
				ast->resultVariable = argument;
			}
			{
				auto argument = make_shared<AstSymbolDeclaration>();
				argument->parent = ast;
				if (cps && cps->stateName)
				{
					argument->composedName = cps->stateName->GetComposedName();
				}
				else
				{
					argument->composedName = "$state";
				}
				ast->arguments.push_back(argument);
				ast->stateArgument = argument;
			}
			if (category && category->signalName)
			{
				auto argument = make_shared<AstSymbolDeclaration>();
				argument->parent = ast;
				argument->composedName = category->signalName->GetComposedName();
				ast->arguments.push_back(argument);
				ast->signalArgument = argument;
			}
			if (bodyName)
			{
				auto argument = bodyName->CreateAst(ast).first;
				ast->arguments.push_back(argument);
				ast->blockBodyArgument = argument;
			}

			for (auto it = name.begin(); it != name.end(); it++)
			{
				auto pair = (*it)->CreateAst(ast);
				if (pair.first)
				{
					ast->readArgumentAstMap.insert(make_pair(it - name.begin(), ast->arguments.size()));
					ast->arguments.push_back(pair.first);
				}
				if (pair.second)
				{
					ast->writeArgumentAstMap.insert(make_pair(it - name.begin(), ast->arguments.size()));
					ast->arguments.push_back(pair.second);
				}
			}

			{
				auto argument = make_shared<AstSymbolDeclaration>();
				argument->parent = ast;
				if (cps && cps->continuationName)
				{
					argument->composedName = cps->continuationName->GetComposedName();
				}
				else
				{
					argument->composedName = "$continuation";
				}
				ast->arguments.push_back(argument);
				ast->continuationArgument = argument;
			}
			return ast;
		}

		/*************************************************************
		Expression::GenerateAst
		*************************************************************/

		SymbolAstResult LiteralExpression::GenerateAst(shared_ptr<SymbolAstScope> scope, SymbolAstContext& context, shared_ptr<SymbolModule> module)
		{
			switch (token.type)
			{
			case CodeTokenType::Integer:
				{
					char* endptr = 0;
					int result = strtol(token.value.c_str(), &endptr, 10);
					auto ast = make_shared<AstIntegerExpression>();
					ast->value = result;
					return SymbolAstResult(ast);
				}
			case CodeTokenType::Float:
				{
					char* endptr = 0;
					double result = strtod(token.value.c_str(), &endptr);
					auto ast = make_shared<AstFloatExpression>();
					ast->value = result;
					return SymbolAstResult(ast);
				}
			case CodeTokenType::String:
				{
					auto ast = make_shared<AstStringExpression>();
					ast->value = token.value;
					return SymbolAstResult(ast);
				}
			}
			return SymbolAstResult();
		}

		SymbolAstResult ArgumentExpression::GenerateAst(shared_ptr<SymbolAstScope> scope, SymbolAstContext& context, shared_ptr<SymbolModule> module)
		{
			return SymbolAstResult();
		}

		SymbolAstResult ReferenceExpression::GenerateAst(shared_ptr<SymbolAstScope> scope, SymbolAstContext& context, shared_ptr<SymbolModule> module)
		{
			return SymbolAstResult();
		}

		SymbolAstResult InvokeExpression::GenerateAst(shared_ptr<SymbolAstScope> scope, SymbolAstContext& context, shared_ptr<SymbolModule> module)
		{
			return SymbolAstResult();
		}

		SymbolAstResult ListExpression::GenerateAst(shared_ptr<SymbolAstScope> scope, SymbolAstContext& context, shared_ptr<SymbolModule> module)
		{
			vector<SymbolAstResult> results;
			for (auto element : elements)
			{
				results.push_back(element->GenerateAst(scope, context, module));
			}
			auto result = SymbolAstResult::Merge(context, results);

			auto ast = make_shared<AstNewArrayLiteralExpression>();
			for (auto& element : results)
			{
				ast->elements.push_back(element.value);
				element.value->parent = ast;
			}
			return result.ReplaceValue(ast);
		}

		SymbolAstResult UnaryExpression::GenerateAst(shared_ptr<SymbolAstScope> scope, SymbolAstContext& context, shared_ptr<SymbolModule> module)
		{
			AstUnaryOperator astOp = AstUnaryOperator::Not;
			switch (op)
			{
			case UnaryOperator::Not:
				astOp = AstUnaryOperator::Not;
				break;
			case UnaryOperator::Positive:
				astOp = AstUnaryOperator::Positive;
				break;
			case UnaryOperator::Negative:
				astOp = AstUnaryOperator::Negative;
				break;
			}

			auto result = operand->GenerateAst(scope, context, module);
			auto ast = make_shared<AstUnaryExpression>();
			ast->op = astOp;
			ast->operand = result.value;
			result.value->parent = ast;
			return result.ReplaceValue(ast);
		}

		SymbolAstResult BinaryExpression::GenerateAst(shared_ptr<SymbolAstScope> scope, SymbolAstContext& context, shared_ptr<SymbolModule> module)
		{
			AstBinaryOperator astOp = AstBinaryOperator::Concat;
			
			switch (op)
			{
			case BinaryOperator::Concat:
				astOp = AstBinaryOperator::Concat;
				break;
			case BinaryOperator::Add:
				astOp = AstBinaryOperator::Add;
				break;
			case BinaryOperator::Sub:
				astOp = AstBinaryOperator::Sub;
				break;
			case BinaryOperator::Mul:
				astOp = AstBinaryOperator::Mul;
				break;
			case BinaryOperator::Div:
				astOp = AstBinaryOperator::Div;
				break;
			case BinaryOperator::LT:
				astOp = AstBinaryOperator::LT;
				break;
			case BinaryOperator::GT:
				astOp = AstBinaryOperator::GT;
				break;
			case BinaryOperator::LE:
				astOp = AstBinaryOperator::LE;
				break;
			case BinaryOperator::GE:
				astOp = AstBinaryOperator::GE;
				break;
			case BinaryOperator::EQ:
				astOp = AstBinaryOperator::EQ;
				break;
			case BinaryOperator::NE:
				astOp = AstBinaryOperator::NE;
				break;
			case BinaryOperator::And:
				astOp = AstBinaryOperator::And;
				break;
			case BinaryOperator::Or:
				astOp = AstBinaryOperator::Or;
				break;
			}
			
			vector<SymbolAstResult> results;
			results.push_back(first->GenerateAst(scope, context, module));
			results.push_back(second->GenerateAst(scope, context, module));
			auto result = SymbolAstResult::Merge(context, results);

			auto ast = make_shared<AstBinaryExpression>();
			ast->op = astOp;
			ast->first = results[0].value;
			results[0].value->parent = ast;
			ast->second = results[1].value;
			results[1].value->parent = ast;
			return result.ReplaceValue(ast);
		}

		/*************************************************************
		Statement::GenerateAst
		*************************************************************/

		/*************************************************************
		GenerateAst
		*************************************************************/

		typedef multimap<SymbolFunction::Ptr, SymbolFunction::Ptr>			MultipleDispatchMap;
		typedef map<SymbolFunction::Ptr, SymbolModule::Ptr>					FunctionModuleMap;
		typedef map<SymbolFunction::Ptr, AstFunctionDeclaration::Ptr>		FunctionAstMap;

		void GenerateStaticAst(
			SymbolAssembly::Ptr symbolAssembly,
			AstAssembly::Ptr assembly,
			SymbolAstScope::Ptr scope,
			MultipleDispatchMap& mdc,
			FunctionModuleMap& functionModules,
			FunctionAstMap& functionAsts
			)
		{
			for (auto module : symbolAssembly->symbolModules)
			{
				for (auto dfp : module->declarationFunctions)
				{
					functionModules.insert(make_pair(dfp.second, module));
					if (!dfp.second->multipleDispatchingRoot.expired())
					{
						mdc.insert(make_pair(dfp.second->multipleDispatchingRoot.lock(), dfp.second));
					}
				}
			}

			for (auto module : symbolAssembly->symbolModules)
			{
				map<Declaration::Ptr, AstDeclaration::Ptr> decls;
				for (auto sdp : module->symbolDeclarations)
				{
					auto it = decls.find(sdp.second);
					if (it == decls.end())
					{
						auto ast = sdp.second->GenerateAst(scope, module, AstNode::WeakPtr(assembly));
						assembly->declarations.push_back(ast);
						decls.insert(make_pair(sdp.second, ast));
						scope->readAsts.insert(make_pair(sdp.first, ast));

						auto itfunc = module->declarationFunctions.find(sdp.second);
						if (itfunc != module->declarationFunctions.end())
						{
							functionAsts.insert(make_pair(itfunc->second, dynamic_pointer_cast<AstFunctionDeclaration>(ast)));
						}
					}
					else
					{
						scope->readAsts.insert(make_pair(sdp.first, it->second));
					}
				}
			}
			
			for (auto module : symbolAssembly->symbolModules)
			{
				for (auto sdp : module->symbolDeclarations)
				{
					if (auto typeDecl = dynamic_pointer_cast<TypeDeclaration>(sdp.second))
					{
						if (typeDecl->parent)
						{
							auto type = scope->readAsts.find(sdp.first)->second;
							auto baseType = scope->GetType(module->baseTypes.find(typeDecl)->second, type);
							type->parent = baseType;
						}
					}
				}
			}
		}

		void FillMultipleDispatchRedirectAst(
			AstFunctionDeclaration::Ptr ast,
			AstFunctionDeclaration::Ptr function
			)
		{
			auto astBlock = make_shared<AstBlockStatement>();
			astBlock->parent = ast;
			ast->statement = astBlock;

			auto astExprStat = make_shared<AstExpressionStatement>();
			astExprStat->parent = astBlock;
			astBlock->statements.push_back(astExprStat);

			auto astInvoke = make_shared<AstInvokeExpression>();
			astInvoke->parent = astExprStat;
			astExprStat->expression = astInvoke;

			auto astFunction = make_shared<AstReferenceExpression>();
			astFunction->parent = astInvoke;
			astFunction->reference = function;
			astInvoke->function = astFunction;

			for (auto argument : ast->arguments)
			{
				auto astArgument = make_shared<AstReferenceExpression>();
				astArgument->parent = astInvoke;
				astArgument->reference = argument;
				astInvoke->arguments.push_back(astArgument);
			}
		}

		void FillMultipleDispatchStepAst(
			AstFunctionDeclaration::Ptr ast,
			string functionName,
			int dispatch
			)
		{
			auto astBlock = make_shared<AstBlockStatement>();
			astBlock->parent = ast;
			ast->statement = astBlock;

			auto astExprStat = make_shared<AstExpressionStatement>();
			astExprStat->parent = astBlock;
			astBlock->statements.push_back(astExprStat);

			auto astInvoke = make_shared<AstInvokeExpression>();
			astInvoke->parent = astExprStat;
			astExprStat->expression = astInvoke;

			auto astFieldAccess = make_shared<AstFieldAccessExpression>();
			astFieldAccess->parent = astInvoke;
			astFieldAccess->composedFieldName = functionName;
			astInvoke->function = astFieldAccess;

			auto astTargetObject = make_shared<AstReferenceExpression>();
			astTargetObject->parent = astFieldAccess;
			astTargetObject->reference = ast->arguments[dispatch];
			astFieldAccess->target = astTargetObject;

			for (auto argument : ast->arguments)
			{
				auto astArgument = make_shared<AstReferenceExpression>();
				astArgument->parent = astInvoke;
				astArgument->reference = argument;
				astInvoke->arguments.push_back(astArgument);
			}
		}

		void GenerateMultipleDispatchAsts(
			SymbolAssembly::Ptr symbolAssembly,
			AstAssembly::Ptr assembly,
			SymbolAstScope::Ptr scope,
			MultipleDispatchMap& mdc,
			FunctionModuleMap& functionModules,
			FunctionAstMap& functionAsts
			)
		{
			auto it = mdc.begin();
			while (it != mdc.end())
			{
				auto lower = mdc.lower_bound(it->first);
				auto upper = mdc.lower_bound(it->second);
				auto module = functionModules.find(it->first)->second;
				auto rootFunc = it->first;
				auto rootAst = functionAsts.find(rootFunc)->second;

				set<int> dispatches;
				AstFunctionDeclaration::Ptr dispatchFailAst;
				for (it = lower; it != upper; it++)
				{
					auto func = it->second;
					for (auto ita = func->function->name.begin(); ita != func->function->name.end(); ita++)
					{
						if (func->argumentTypes.find(*ita) != func->argumentTypes.end())
						{
							dispatches.insert(ita - func->function->name.begin());
						}
					}
				}
				
				FillMultipleDispatchStepAst(rootAst, "$dispatch<>" + rootAst->composedName, rootAst->readArgumentAstMap.find(*dispatches.begin())->second);
				{
					auto ast = rootFunc->function->GenerateAst(scope, module, assembly);
					ast->composedName = "$dispatch_fail<>" + rootAst->composedName;
					assembly->declarations.push_back(ast);
					dispatchFailAst = dynamic_pointer_cast<AstFunctionDeclaration>(ast);
				}

				set<string> createdFunctions, typeFunctions, objectFunctions;
				for (it = lower; it != upper; it++)
				{
					auto func = it->second;
					auto module = functionModules.find(func)->second;
					string signature;
					for (auto itd = dispatches.begin(); itd != dispatches.end(); itd++)
					{
						string methodName = "$dispatch<" + signature + ">" + rootAst->composedName;
						auto ast = dynamic_pointer_cast<AstFunctionDeclaration>(func->function->GenerateAst(scope, module, assembly));
						ast->composedName = methodName;

						auto ita = func->argumentTypes.find(func->function->name[*itd]);
						if (ita == func->argumentTypes.end())
						{
							auto type = make_shared<AstPredefinedType>();
							type->parent = ast;
							type->typeName = AstPredefinedTypeName::Object;
							ast->ownerType = type;
							objectFunctions.insert(methodName);
						}
						else
						{
							ast->ownerType = scope->GetType(ita->second, ast);
							typeFunctions.insert(methodName);
						}

						if (itd != dispatches.begin())
						{
							signature += ", ";
						}
						{
							stringstream o;
							ast->ownerType->Print(o, 0);
							signature += o.str();
						}

						string functionName = "$dispatch<" + signature + ">" + rootAst->composedName;
						if (createdFunctions.insert(functionName).second)
						{
							assembly->declarations.push_back(ast);
							auto itd2 = itd;
							if (++itd2 == dispatches.end())
							{
								FillMultipleDispatchRedirectAst(ast, functionAsts.find(func)->second);
							}
							else
							{
								FillMultipleDispatchStepAst(ast, functionName, ast->readArgumentAstMap.find(*itd)->second);
							}
						}
					}
				}

				for (auto name : typeFunctions)
				{
					if (objectFunctions.find(name) == objectFunctions.end())
					{
						auto ast = dynamic_pointer_cast<AstFunctionDeclaration>(rootFunc->function->GenerateAst(scope, module, assembly));
						ast->composedName = name;
						{
							auto type = make_shared<AstPredefinedType>();
							type->parent = ast;
							type->typeName = AstPredefinedTypeName::Object;
							ast->ownerType = type;
						}
						assembly->declarations.push_back(ast);
						FillMultipleDispatchRedirectAst(ast, dispatchFailAst);
					}
				}

				functionAsts.find(rootFunc)->second = dispatchFailAst;
				it = upper;
			}
		}

		ast::AstAssembly::Ptr GenerateAst(SymbolAssembly::Ptr symbolAssembly)
		{
			auto assembly = make_shared<AstAssembly>();
			auto scope = make_shared<SymbolAstScope>();

			multimap<SymbolFunction::Ptr, SymbolFunction::Ptr> multipleDispatchChildren;
			map<SymbolFunction::Ptr, SymbolModule::Ptr> functionModules;
			map<SymbolFunction::Ptr, AstFunctionDeclaration::Ptr> functionAsts;
			GenerateStaticAst(symbolAssembly, assembly, scope, multipleDispatchChildren, functionModules, functionAsts);
			GenerateMultipleDispatchAsts(symbolAssembly, assembly, scope, multipleDispatchChildren, functionModules, functionAsts);

			return assembly;
		}
	}
}