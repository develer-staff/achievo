<?php
 /*
  * @version $Revision$
  * @Author Ivo Jansch <ivo@achievo.org>
  *
  * Sends an email reminder if someone did not enter all of his hours in
  * the previous week.
  * 
  * $Id$
  *
  */

  // work from achievo dir. .
  chdir("../");
  include_once("atk.inc");
  include_once("achievotools.inc");

  $today = date("Y-m-d");
  $year = substr($today,0,4);
  $month = substr($today,5,2);
  $day = substr($today,8,2);
  $incomplete_weeks = array();

  // get emailaddresses for all users and presort them
  $query = "SELECT
              id, userid, firstname, lastname, email
            FROM
              person
            WHERE
              status = 'active' AND role='employee'
           ";

  $users = $g_db->getrows($query);
  for ($i=0;$i<count($users);$i++)
  {
    $userInfo[$users[$i]["id"]]["mail"] = $users[$i]["email"];
    $userInfo[$users[$i]["id"]]["name"] = $users[$i]["firstname"].' '.$users[$i]["lastname"];
  }

  $weekcheck = atkconfig("timereg_checkweeks");
  if ($weekcheck=="") $weekcheck=1;
  for ($weekloop=1;$weekloop<=$weekcheck;$weekloop++)
  {
    // generate a date in the correct week (doesn't matter which, so day-7 should
    // always work.
    $prevweekstamp = mktime(12,12,12,$month,$day-($weekloop*7),$year);

    $startdate = startOfWeek(date("Y-m-d",$prevweekstamp));
    $enddate =  endOfWeek(date("Y-m-d",$prevweekstamp));

    // remember total checking period
    if ($weekloop==1) $globalend = $enddate;
    if ($weekloop==$weekcheck) $globalstart = $startdate;

    // reset time array.
    $time = array();

    $week = strftime("%V",$prevweekstamp);

    // get all contracts.
    $query = "SELECT
                uc_hours, person.id, email, supervisor
              FROM
                usercontract, person
              WHERE
                startdate <= '$startdate'
                AND enddate > '$startdate'
                AND usercontract.userid = person.id
                AND person.status='active' AND person.role = 'employee'";

    $contracts = $g_db->getrows($query);

    for ($i=0;$i<count($contracts);$i++)
    {
      $time[$contracts[$i]["id"]]["contract"] = $contracts[$i]["uc_hours"]*60;
      $time[$contracts[$i]["id"]]["email"] = $contracts[$i]["email"];
      $time[$contracts[$i]["id"]]["supervisor"] = $contracts[$i]["supervisor"];
    }

    // get working hours
    $query = "SELECT
                sum(time) as time, hours.userid
              FROM
                hours, person
              WHERE
                hours.userid = person.id
                AND person.status = 'active'
                AND person.role='employee'
                AND activitydate between '$startdate' and '$enddate'
              GROUP BY hours.userid";

    $hours = $g_db->getrows($query);
    for ($i=0;$i<count($hours);$i++)
    {
      $user = $hours[$i]["userid"];
      $hoursthisweek = $hours[$i]["time"];
      $time[$user]["time"] = $hoursthisweek;
    }

    // mail people who have entered less time than their contract
    if (is_array($time))
    {
      foreach($time as $user => $data)
      {
        if ($data["time"]<$data["contract"]) // tijdelijk maar een mailtje.
        {
          $incomplete_weeks[$user][] = array("week"=>$week,
                                             "missing"=>time_format($data["contract"]-$data["time"]),
                                             "startdate"=>$startdate,
                                             "enddate"=>$enddate);
        }
      }
    }
  }

  foreach ($incomplete_weeks as $user => $data)
  {
    $body = stringparse(text("timeguard_mail_header"),array("userid"=>$userInfo[$user]["name"],
                                                     "startdate"=>$globalstart,
                                                     "enddate"=>$globalend))."\n";

    for ($i=0;$i<count($data);$i++)
    {
      $body.= "\n".stringparse(text("timeguard_mail_line"),array("hours"=>$data[$i]["missing"],
                                                            "week"=>$data[$i]["week"],
                                                            "startdate"=>$data[$i]["startdate"],
                                                            "enddate"=>$data[$i]["enddate"]));
    }

    $to = $userInfo[$user]["mail"];
    $cc = "";
    $supervisormail = $userInfo[$time[$user]["supervisor"]]["mail"];
    if ($supervisormail !="")
    {
      $cc = "Cc: ".$supervisormail."\r\n";
    }
    if ($to!="")
    {
      usermail($to,text("timeguard_mail_subject"),$body, $cc);
      echo "sent mail to $to";
      if ($cc!="") echo " and a cc to $cc";
      echo "\n";
    }
    else
    {
      if ($cc=="")
      {
        echo "would've sent mail to $user, but he doesn't have an email address\n";
      }
      else
      {
        usermail($cc,text("timeguard_mail_subject"),$body);
        echo "would've sent mail to $user, but he doesn't have an email address. Sending only to supervisor\n";
      }
    }
  }

?>
