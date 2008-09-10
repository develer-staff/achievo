<?php
  /**
   * This file is part of the Achievo ATK distribution.
   * Detailed copyright and licensing information can be found
   * in the doc/COPYRIGHT and doc/LICENSE files which should be
   * included in the distribution.
   *
   * @package atk
   * @subpackage utils
   *
   * @copyright (c)2005 Ibuildings.nl BV
   * @license http://www.achievo.org/atk/licensing ATK Open Source License
   *
   * @version $Revision: 1.10 $
   * $Id: class.atktmpfile.inc,v 1.10 2007/06/18 14:45:48 peter Exp $
   */

  /**
   * This class can be used to create, read and remove temporary files.
   * The files are stored in ATK's temporary directory.
   * An ideal application of this class is writing small php include files
   * with cached data.
   *
   * @author Ivo Jansch <ivo@achievo.org>
   * @package atk
   * @subpackage utils
   *
   */
  class atkTmpFile
  {
    var $m_filename;
    var $m_fp;
    var $m_mode;

    function atkTmpFile($filename)
    {
      $this->m_filename = $filename;
    }

    function exists()
    {
      return file_exists($this->getPath());
    }

    function read()
    {
      if ($this->exists())
      {
        return file($this->getPath());
      }
    }
    
    function readFile()
    {
      if ($this->exists())
      {
        if (function_exists("file_get_contents"))
          return file_get_contents($this->getPath());
        else return implode(null, $this->read());
      }
    }

    function writeFile($data)
    {
      if ($this->open("w"))
      {
        $this->write($data);
        $this->close();
        return true;
      }

      return false;
    }

    /**
     * Recursively creates the directory structure for this file.
     */
    function createDirectoryStructure()
    {
      $file = atkconfig("atktempdir").$this->m_filename;
      $path = dirname($file);
      useattrib('atkfileattribute');
      atkFileAttribute::mkdir($path);
    }
    
    function writeAsPhp($varname, $data)
    {
      $res = "<?php\n";
      $res.= "\$".$varname." = ".var_export($data, true);
      $res.= "\n?>";

      return $this->writeFile($res);
    }

    function open($mode)
    {
      if ($this->m_mode!="" && $this->m_mode!=$mode)
      {
        // file is already open in different mode, close first
        $this->close();
      }
      if (is_null($this->m_fp))
      {
        if ($mode != 'r' && $mode != 'r+')
        {
          $this->createDirectoryStructure();  
        }
        
        $this->m_fp = fopen($this->getPath(), $mode);
        $this->m_mode = $mode;
      }
      return !is_null($this->m_fp);
    }

    function write($data)
    {
      fwrite($this->m_fp, $data);
    }

    function close()
    {
      if (!is_null($this->m_fp))
      {
        fclose($this->m_fp);
        $this->m_mode="";
        $this->m_fp = null;
      }
    }
    
    /**
     * Send the file contents directly to the browser.
     *
     * @return boolean True if succesful, false on error.
     */
    function fpassthru()
    {
      if ($this->open("r"))
      {
        fpassthru($this->m_fp);      
        $this->close();
      }
      return true;
    }

    function remove()
    {
      $this->close();
      unlink($this->getPath());
    }

    /**
     * Returns the time the file was last changed, or FALSE in case of an error.
     * The time is returned as a Unix timestamp.
     *
     * @return int last changed timestamp
     */
    function filecTime()
    {
      if($this->exists()) return filectime($this->getPath());
      return false;
    }

    /**
     * Returns the file age in seconds
     *
     * @return int fileage in seconds
     */
    function fileAge()
    {
      $filectime = $this->filectime();
      if($filectime!=false) return (time()-$filectime);
      return false;
    }



    function getPath()
    {
      return atkconfig("atktempdir").$this->m_filename;
    }
  }