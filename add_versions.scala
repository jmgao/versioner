import java.io.PrintWriter
import scala.io.Source
import scala.util.control.Exception

case class Command(version: Int, symbolName: String, filename: String, lineNumber: Int)
object Command {
  def parse(str: String) : Command = {
    val parts = str.split(":")
    if (parts.size != 4) {
      throw new RuntimeException("failed to parse command: %s".format(str));
    }

    Command(parts(0).toInt, parts(1), parts(2), parts(3).toInt)
  }
}

def patch(filename: String, lines: Array[String], startLine: Int, endLine: Int, version: Int) = {
  val patchedLine = lines(patchLine - 1).replace(";", " __INTRODUCED_IN(%d);".format(version))
  lines(patchLine - 1) = patchedLine
}

def getEndLine(filename: String, lines: Array[String], startLine: Int): Int = {
  for (lineNumber <- Stream.from(startLine)) {
    if (lineNumber > lines.size) {
      println("failed to find trailing semicolon at %s:%d".format(filename, startLine))
      System.exit(1)
    }

    val count = lines(lineNumber - 1).count(_ == ';')
    if (count == 1) {
      return lineNumber
    } else if (count > 1) {
      println("multiple semicolons at %s:%d".format(filename, lineNumber))
    }
  }

  -1
}

if (args.size != 1) {
  println("usage: add_versions.scala VERSION_COMMANDS");
  System.exit(1)
}

Exception.allCatch.either {
  Source.fromFile(args(0))
}

match {
  case Left(ex) =>
    println("failed to open version commands: %s".format(ex.getMessage))
    System.exit(1)

  case Right(cmdSrc) =>
    val cmds = cmdSrc.getLines().map(Command.parse)
    val grouped = cmds.toSeq.groupBy { _.filename }
    for ((file, cmds) <- grouped) {
      println("%s: ".format(file))
      val sortedCmds = cmds.sortBy(_.lineNumber)

      val targetFile = Exception.allCatch.either {
        Source.fromFile(file)
      }

      if (targetFile.isLeft) {
        println("failed to open file %s: %s".format(file, targetFile.left))
        System.exit(1)
      }

      val Right(lines) = targetFile
      val linesArray = lines.getLines.toArray

      sortedCmds.foreach { cmd => {
        val patchLine = getEndLine(file, linesArray, cmd.lineNumber)
        println("    %d => %s".format(patchLine, cmd))
        patch(file, linesArray, cmd.lineNumber, patchLine, cmd.version)
      }}

      val writer = new PrintWriter(file)
      linesArray foreach { writer.println(_) }
      writer.close()
    }
}
